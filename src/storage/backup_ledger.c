#include "tired/backup_ledger.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/private_file.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
bool tired_backup_ledger_read(TiredDirectory *transaction, size_t index, bool *found, char uuid[37],
                              TiredFileFingerprint *before, TiredError *error)
{
    *found = false;
    if (index == 0 || index > 5)
        return tired_error_set(error, TIRED_INVALID, "backup-index", "Invalid backup ledger index.",
                               0);
    char filename[32];
    (void)snprintf(filename, sizeof(filename), "backup-%zu.json", index);
    TiredText bytes = {0};
    if (!tired_private_file_read(transaction, filename, 8192, &bytes, error))
    {
        if (error->status != TIRED_NOT_FOUND)
            return false;
        tired_error_clear(error);
        return true;
    }
    struct json_object *document = NULL, *id = NULL, *fingerprint = NULL;
    bool ok =
        tired_json_parse(bytes.data, bytes.length, 8192, &document, error) &&
        json_object_is_type(document, json_type_object) &&
        json_object_object_length(document) == 2 &&
        json_object_object_get_ex(document, "rollback_uuid", &id) &&
        json_object_is_type(id, json_type_string) &&
        tired_uuid_valid(json_object_get_string(id), (size_t)json_object_get_string_len(id)) &&
        json_object_object_get_ex(document, "before", &fingerprint);
    const char *encoded =
        ok ? json_object_to_json_string_ext(fingerprint, JSON_C_TO_STRING_PLAIN) : NULL;
    ok = encoded != NULL && tired_file_fingerprint_parse(encoded, strlen(encoded), before, error) &&
         before->exists;
    if (ok)
    {
        memcpy(uuid, json_object_get_string(id), 37);
        *found = true;
    }
    json_object_put(document);
    tired_text_destroy(&bytes);
    return ok || (error->status != TIRED_OK
                      ? false
                      : tired_error_set(error, TIRED_RECOVERY_REQUIRED, "backup-ledger-invalid",
                                        "Rollback backup declaration is invalid.", 0));
}
bool tired_backup_ledger_clear(TiredDirectory *transaction, TiredDirectory *artifacts,
                               TiredError *error)
{
    for (size_t i = 1; i <= 5; ++i)
    {
        bool found = false;
        char uuid[37] = {0};
        TiredFileFingerprint before = {0}, actual = {0};
        if (!tired_backup_ledger_read(transaction, i, &found, uuid, &before, error))
            return false;
        if (!found)
            continue;
        if (!tired_file_fingerprint(artifacts, uuid, TIRED_PRIVATE_FILE_LIMIT, &actual, error))
            return false;
        if (!actual.exists)
            continue;
        if (actual.uid != geteuid() || actual.mode != 0600 || actual.size != before.size ||
            strcmp(actual.sha256, before.sha256) != 0 || !tired_directory_check(artifacts, error))
            return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "backup-cleanup-conflict",
                                   "A rollback backup changed; it was not removed.", 0);
        if (unlinkat(tired_directory_fd(artifacts), uuid, 0) != 0 ||
            fsync(tired_directory_fd(artifacts)) != 0)
            return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "backup-cleanup-io",
                                   "Cannot finish checked backup cleanup.", errno);
    }
    return true;
}
