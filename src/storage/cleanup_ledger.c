#define _GNU_SOURCE
#include "tired/file_fingerprint.h"
#include "tired/history.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/private_file.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool failed(TiredError *error)
{
    return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "cleanup-ledger-conflict",
                           "Committed service state is preserved. Cleanup found a changed file or "
                           "directory; inspect the current transaction and select finish.",
                           errno);
}
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
bool tired_transaction_cleanup_pending(TiredDirectory *directory, bool *pending, TiredError *error)
{
    TiredFileFingerprint fingerprint = {0};
    TiredText bytes = {0};
    bool ok = tired_file_fingerprint(directory, "cleanup.pending", 8, &fingerprint, error);
    *pending = ok && fingerprint.exists;
    if (*pending)
        ok = tired_private_file_read(directory, "cleanup.pending", 8, &bytes, error) &&
             bytes.length == 8 && memcmp(bytes.data, "cleanup\n", 8) == 0;
    tired_text_destroy(&bytes);
    return ok || (error->status != TIRED_OK ? false : failed(error));
}
bool tired_transaction_cleanup_mark(TiredDirectory *directory, TiredError *error)
{
    bool pending = false;
    return tired_transaction_cleanup_pending(directory, &pending, error) &&
           (pending ||
            tired_private_file_create(directory, "cleanup.pending", "cleanup\n", 8, error));
}
bool tired_transaction_cleanup_clear(TiredDirectory *directory, TiredError *error)
{
    bool pending = false;
    return tired_transaction_cleanup_pending(directory, &pending, error) &&
           (!pending || (tired_directory_check(directory, error) &&
                         unlinkat(tired_directory_fd(directory), "cleanup.pending", 0) == 0 &&
                         fsync(tired_directory_fd(directory)) == 0));
}
bool tired_cleanup_record(struct json_object *rows, const char *uuid, const char *area,
                          const char *name, TiredDirectory *directory, bool remove_directory,
                          TiredError *error)
{
    struct stat identity;
    TiredFileFingerprint fingerprint = {0};
    TiredText bytes = {0};
    struct json_object *row = json_object_new_object(), *fp = NULL;
    bool ok = row != NULL && json_object_array_length(rows) < 8192 &&
              tired_directory_check(directory, error) &&
              fstat(tired_directory_fd(directory), &identity) == 0 &&
              add(row, "transaction_uuid", json_object_new_string(uuid)) &&
              add(row, "area", json_object_new_string(area)) &&
              add(row, "name", json_object_new_string(name)) &&
              add(row, "directory_device", json_object_new_uint64(identity.st_dev)) &&
              add(row, "directory_inode", json_object_new_uint64(identity.st_ino)) &&
              add(row, "remove_directory", json_object_new_boolean(remove_directory));
    if (ok && !remove_directory)
        ok = tired_file_fingerprint(directory, name, TIRED_PRIVATE_FILE_LIMIT, &fingerprint,
                                    error) &&
             fingerprint.exists && fingerprint.uid == geteuid() && fingerprint.mode == 0600 &&
             tired_file_fingerprint_encode(&fingerprint, &bytes, error) &&
             tired_json_parse(bytes.data, bytes.length, 4096, &fp, error);
    if (ok)
    {
        ok = remove_directory ? json_object_object_add(row, "fingerprint", NULL) == 0
                              : add(row, "fingerprint", fp);
        fp = NULL;
    }
    if (ok && json_object_array_add(rows, row) == 0)
        row = NULL;
    else
        ok = false;
    json_object_put(row);
    json_object_put(fp);
    tired_text_destroy(&bytes);
    return ok || (error->status != TIRED_OK ? false : failed(error));
}
bool tired_cleanup_save(TiredDirectory *directory, const TiredTransactionRecord *anchor,
                        struct json_object *rows, TiredError *error)
{
    struct json_object *document = json_object_new_object();
    bool ok = document != NULL && add(document, "schema_version", json_object_new_int(1)) &&
              add(document, "transaction_uuid", json_object_new_string(anchor->transaction_uuid)) &&
              add(document, "service_uuid", json_object_new_string(anchor->service_uuid)) &&
              add(document, "approved_sha256", json_object_new_string(anchor->approved_sha256)) &&
              add(document, "rows", json_object_get(rows));
    const char *bytes =
        ok ? json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN) : NULL;
    ok = bytes != NULL && strlen(bytes) <= TIRED_PRIVATE_FILE_LIMIT &&
         tired_private_file_create(directory, "cleanup.json", bytes, strlen(bytes), error);
    json_object_put(document);
    return ok;
}
static const char *string(struct json_object *object, const char *key)
{
    struct json_object *value = NULL;
    return json_object_object_get_ex(object, key, &value) &&
                   json_object_is_type(value, json_type_string)
               ? json_object_get_string(value)
               : NULL;
}
static bool safe_name(const char *area, const char *name)
{
    size_t length = strlen(name);
    if (length == 0 || length > 255 || strchr(name, '/') != NULL || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return false;
    if (strcmp(area, "artifacts") == 0)
        return tired_uuid_valid(name, length);
    if (strcmp(area, "journal") == 0)
    {
        if (length != 9 || strcmp(name + 4, ".json") != 0)
            return false;
        for (size_t i = 0; i < 4; ++i)
            if (name[i] < '0' || name[i] > '9')
                return false;
        return true;
    }
    if (area[0] != '\0')
        return false;
    if (length == 18 && memcmp(name, "effect-", 7) == 0 && name[11] == '-' &&
        (name[12] == '1' || name[12] == '2') && strcmp(name + 13, ".json") == 0)
    {
        for (size_t i = 7; i < 11; ++i)
            if (name[i] < '0' || name[i] > '9')
                return false;
        return true;
    }
    if (strcmp(name, "request.json") == 0 || strcmp(name, "files.json") == 0 ||
        strcmp(name, "before.json") == 0 || strcmp(name, "receipt.json") == 0 ||
        strcmp(name, "cleanup.json") == 0)
        return true;
    return (length == 12 && memcmp(name, "stage-", 6) == 0 && name[6] >= '1' && name[6] <= '5' &&
            strcmp(name + 7, ".json") == 0) ||
           (length == 13 && memcmp(name, "backup-", 7) == 0 && name[7] >= '1' && name[7] <= '5' &&
            strcmp(name + 8, ".json") == 0);
}
static bool apply(const TiredLayout *layout, struct json_object *row, TiredError *error)
{
    const char *uuid = string(row, "transaction_uuid"), *area = string(row, "area"),
               *name = string(row, "name");
    struct json_object *dev = NULL, *ino = NULL, *remove = NULL, *fp = NULL;
    uint64_t device, inode;
    bool valid =
        json_object_is_type(row, json_type_object) && json_object_object_length(row) == 7 &&
        uuid != NULL && tired_uuid_valid(uuid, strlen(uuid)) && area != NULL &&
        (area[0] == '\0' || strcmp(area, "journal") == 0 || strcmp(area, "artifacts") == 0) &&
        name != NULL && json_object_object_get_ex(row, "remove_directory", &remove) &&
        json_object_is_type(remove, json_type_boolean) &&
        json_object_object_get_ex(row, "fingerprint", &fp) &&
        json_object_object_get_ex(row, "directory_device", &dev) &&
        tired_json_u64(dev, 0, UINT64_MAX, &device, error) &&
        json_object_object_get_ex(row, "directory_inode", &ino) &&
        tired_json_u64(ino, 1, UINT64_MAX, &inode, error);
    bool directory_removal = valid && json_object_get_boolean(remove);
    valid = valid && (directory_removal ? name[0] == '\0' && fp == NULL
                                        : fp != NULL && safe_name(area, name));
    if (!valid)
        return failed(error);
    TiredDirectory *root = NULL, *transaction = NULL, *child = NULL;
    bool ok = tired_directory_open(layout->paths[TIRED_PATH_TRANSACTIONS].data, geteuid(), true,
                                   &root, error);
    if (ok && !tired_directory_child(root, uuid, false, true, &transaction, error))
    {
        ok = error->status == TIRED_NOT_FOUND;
        if (ok)
            tired_error_clear(error);
        goto done;
    }
    if (ok && area[0] != '\0' &&
        !tired_directory_child(transaction, area, false, true, &child, error))
    {
        ok = error->status == TIRED_NOT_FOUND;
        if (ok)
            tired_error_clear(error);
        goto done;
    }
    TiredDirectory *target = area[0] == '\0' ? transaction : child;
    struct stat identity;
    ok = ok && fstat(tired_directory_fd(target), &identity) == 0 &&
         (uint64_t)identity.st_dev == device && (uint64_t)identity.st_ino == inode;
    if (ok && directory_removal)
    {
        TiredDirectory *parent = area[0] == '\0' ? root : transaction;
        ok = tired_directory_check(target, error) &&
             unlinkat(tired_directory_fd(parent), area[0] == '\0' ? uuid : area, AT_REMOVEDIR) ==
                 0 &&
             fsync(tired_directory_fd(parent)) == 0;
    }
    else if (ok)
    {
        TiredFileFingerprint expected = {0}, actual = {0};
        const char *bytes = json_object_to_json_string_ext(fp, JSON_C_TO_STRING_PLAIN);
        ok = bytes != NULL &&
             tired_file_fingerprint_parse(bytes, strlen(bytes), &expected, error) &&
             expected.exists && expected.uid == geteuid() && expected.mode == 0600 &&
             tired_file_fingerprint(target, name, TIRED_PRIVATE_FILE_LIMIT, &actual, error);
        if (ok && actual.exists)
            ok = tired_file_fingerprint_equal(&expected, &actual) &&
                 tired_directory_check(target, error) &&
                 unlinkat(tired_directory_fd(target), name, 0) == 0 &&
                 fsync(tired_directory_fd(target)) == 0;
    }
done:
    tired_directory_destroy(child);
    tired_directory_destroy(transaction);
    tired_directory_destroy(root);
    return ok || (error->status != TIRED_OK ? false : failed(error));
}
bool tired_transaction_cleanup_resume(const TiredLayout *layout, TiredDirectory *transaction,
                                      const TiredTransactionRecord *anchor,
                                      const TiredBackend *backend, TiredError *error)
{
    TiredText bytes = {0};
    struct json_object *document = NULL, *version = NULL, *rows = NULL;
    uint64_t schema;
    bool ok =
        tired_private_file_read(transaction, "cleanup.json", TIRED_PRIVATE_FILE_LIMIT, &bytes,
                                error) &&
        tired_json_parse(bytes.data, bytes.length, TIRED_PRIVATE_FILE_LIMIT, &document, error);
    const char *uuid = ok ? string(document, "transaction_uuid") : NULL;
    const char *service = ok ? string(document, "service_uuid") : NULL;
    const char *digest = ok ? string(document, "approved_sha256") : NULL;
    ok = ok && json_object_object_length(document) == 5 && uuid != NULL && service != NULL &&
         digest != NULL && strcmp(uuid, anchor->transaction_uuid) == 0 &&
         strcmp(service, anchor->service_uuid) == 0 &&
         strcmp(digest, anchor->approved_sha256) == 0 &&
         json_object_object_get_ex(document, "schema_version", &version) &&
         tired_json_u64(version, 1, 1, &schema, error) &&
         json_object_object_get_ex(document, "rows", &rows) &&
         json_object_is_type(rows, json_type_array) && json_object_array_length(rows) <= 8192;
    for (size_t i = 0; ok && i < json_object_array_length(rows); ++i)
    {
        ok = apply(layout, json_object_array_get_idx(rows, i), error);
        if (backend != NULL && backend->tick != NULL)
            backend->tick(backend->context, "cleanup_entry");
    }
    if (ok)
        ok = tired_transaction_cleanup_clear(transaction, error);
    json_object_put(document);
    tired_text_destroy(&bytes);
    return ok || (error->status != TIRED_OK ? false : failed(error));
}
