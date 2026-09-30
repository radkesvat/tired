#define _GNU_SOURCE
#include "tired/history.h"
#include "tired/file_target.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/private_file.h"
#include "tired/transaction_inventory.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct
{
    TiredText name;
    TiredServiceRecord record;
    TiredFileFingerprint metadata, unit, foreign, foreign_descriptor;
    bool has_foreign;
    bool keep, empty;
} Revision;
static bool conflict(TiredError *error)
{
    return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "history-cleanup-conflict",
                           "Committed service state is preserved. History cleanup found changed or "
                           "unknown files; inspect recover/doctor.",
                           errno);
}
static bool names(TiredDirectory *directory, TiredTextList *output, TiredError *error)
{
    if (!tired_directory_check(directory, error))
        return false;
    int fd =
        openat(tired_directory_fd(directory), ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *scan = fd < 0 ? NULL : fdopendir(fd);
    if (scan == NULL)
    {
        if (fd >= 0)
            close(fd);
        return conflict(error);
    }
    bool ok = true;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (entry == NULL)
        {
            ok &= errno == 0;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!tired_text_list_append(output, entry->d_name, strlen(entry->d_name), 4096,
                                    TIRED_INPUT_LIMIT, error))
        {
            ok = false;
            break;
        }
    }
    if (closedir(scan) != 0)
        ok = false;
    return ok && tired_directory_check(directory, error);
}
static bool remove_file(TiredDirectory *directory, const char *name,
                        const TiredFileFingerprint *expected, TiredError *error)
{
    TiredFileFingerprint actual = {0};
    if (!tired_file_fingerprint(directory, name, TIRED_PRIVATE_FILE_LIMIT, &actual, error))
        return false;
    if (!actual.exists)
        return true;
    if (!tired_file_fingerprint_equal(&actual, expected) ||
        !tired_directory_check(directory, error))
        return conflict(error);
    return (unlinkat(tired_directory_fd(directory), name, 0) == 0 &&
            fsync(tired_directory_fd(directory)) == 0) ||
           conflict(error);
}
static int latest(const void *left, const void *right)
{
    const Revision *a = left, *b = right;
    if (a->record.metadata.updated_usec != b->record.metadata.updated_usec)
        return a->record.metadata.updated_usec > b->record.metadata.updated_usec ? -1 : 1;
    return strcmp(a->name.data, b->name.data);
}
static bool foreign_snapshot(TiredDirectory *directory, Revision *revision, size_t *budget,
                             TiredError *error)
{
    TiredText bytes = {0};
    struct json_object *document = NULL, *sha = NULL;
    bool ok = tired_file_snapshot(directory, "foreign-unit.json", 128,
                                  &revision->foreign_descriptor, &bytes, error) &&
              tired_file_fingerprint(directory, "foreign-unit.service", TIRED_PRIVATE_FILE_LIMIT,
                                     &revision->foreign, error);
    revision->has_foreign = ok && revision->foreign_descriptor.exists;
    if (ok && revision->has_foreign)
        ok = revision->foreign_descriptor.uid == geteuid() &&
             revision->foreign_descriptor.mode == 0600 &&
             tired_json_parse(bytes.data, bytes.length, 128, &document, error) &&
             json_object_object_length(document) == 1 &&
             json_object_object_get_ex(document, "sha256", &sha) &&
             json_object_is_type(sha, json_type_string) && json_object_get_string_len(sha) == 64 &&
             (!revision->foreign.exists ||
              (revision->foreign.uid == geteuid() && revision->foreign.mode == 0600 &&
               strcmp(revision->foreign.sha256, json_object_get_string(sha)) == 0));
    if (ok)
        ok = !revision->foreign.exists || revision->has_foreign;
    if (ok)
    {
        uint64_t charged = bytes.length + (revision->foreign.exists ? revision->foreign.size : 0);
        ok = charged <= *budget;
        if (ok)
            *budget -= (size_t)charged;
    }
    json_object_put(document);
    tired_text_destroy(&bytes);
    return ok || (error->status != TIRED_OK ? false : conflict(error));
}
static bool environment_remove(const TiredLayout *layout, const TiredServiceRecord *record,
                               TiredError *error)
{
    if (!record->has_environment)
        return true;
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_ENVIRONMENT};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    memcpy(target.revision_uuid, record->environment_revision, 37);
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL, *parent = NULL;
    TiredFileFingerprint actual = {0};
    TiredTextList entries = {0};
    TiredText parent_path = {0};
    bool ok = tired_file_target_resolve(layout, &target, &resolved, error);
    if (ok && !tired_directory_open(resolved.directory.data, geteuid(), true, &directory, error))
    {
        ok = error->status == TIRED_NOT_FOUND;
        if (ok)
            tired_error_clear(error);
        goto done;
    }
    if (!ok || !names(directory, &entries, error))
        goto done;
    for (size_t i = 0; i < entries.count; ++i)
        if (strcmp(entries.items[i].data, "environment") != 0)
        {
            ok = conflict(error);
            goto done;
        }
    ok = tired_file_fingerprint(directory, "environment", TIRED_PRIVATE_FILE_LIMIT, &actual, error);
    if (ok && actual.exists)
        ok = actual.uid == geteuid() && actual.mode == 0600 &&
             strcmp(actual.sha256, record->environment_sha256) == 0 &&
             remove_file(directory, "environment", &actual, error);
    if (ok)
    {
        const char *slash = strrchr(resolved.directory.data, '/');
        ok = slash != NULL &&
             tired_text_set(&parent_path, resolved.directory.data,
                            (size_t)(slash - resolved.directory.data), 4096, error) &&
             tired_directory_open(parent_path.data, geteuid(), true, &parent, error) &&
             tired_directory_check(directory, error) &&
             unlinkat(tired_directory_fd(parent), target.revision_uuid, AT_REMOVEDIR) == 0 &&
             fsync(tired_directory_fd(parent)) == 0;
    }
done:
    tired_resolved_file_destroy(&resolved);
    tired_text_list_destroy(&entries);
    tired_text_destroy(&parent_path);
    tired_directory_destroy(parent);
    tired_directory_destroy(directory);
    return ok || (error->status != TIRED_OK ? false : conflict(error));
}
bool tired_history_finalize(const TiredLayout *layout, const TiredMutation *mutation,
                            TiredError *error)
{
    const TiredServiceRecord *current = &mutation->proposed;
    TiredTransactionInventory transactions = {0};
    TiredDirectory *root = NULL, *service = NULL;
    TiredTextList entries = {0};
    Revision *revisions = NULL;
    size_t count = 0;
    size_t budget = 64 * 1024 * 1024;
    bool ok = tired_transaction_inventory_load(layout, &transactions, error) &&
              transactions.complete && transactions.pending_names.count <= 1;
    for (size_t i = 0; ok && i < transactions.count; ++i)
        ok = strcmp(transactions.entries[i].directory_name.data,
                    current->metadata.transaction_uuid) == 0 ||
             (!transactions.entries[i].cleanup_pending &&
              (transactions.entries[i].progress.mode == TIRED_PROGRESS_COMMITTED ||
               transactions.entries[i].progress.mode == TIRED_PROGRESS_ROLLED_BACK));
    if (!ok)
        goto done;
    if (!tired_directory_open(layout->paths[TIRED_PATH_HISTORY].data, geteuid(), true, &root,
                              error) ||
        !tired_directory_child(root, current->metadata.service_uuid, false, true, &service, error))
    {
        ok = error->status == TIRED_NOT_FOUND;
        if (ok)
            tired_error_clear(error);
        goto done;
    }
    ok = names(service, &entries, error);
    if (!ok)
        goto done;
    count = entries.count;
    revisions = calloc(count == 0 ? 1 : count, sizeof(*revisions));
    if (revisions == NULL)
    {
        ok = tired_error_set(error, TIRED_INTERNAL, "allocation",
                             "Cannot inspect retained history.", 0);
        goto done;
    }
    for (size_t i = 0; i < count && ok; ++i)
    {
        Revision *revision = &revisions[i];
        revision->name = entries.items[i];
        entries.items[i] = (TiredText){0};
        TiredDirectory *directory = NULL;
        TiredText bytes = {0};
        TiredTextList files = {0};
        ok = tired_uuid_valid(revision->name.data, revision->name.length) &&
             tired_directory_child(service, revision->name.data, false, true, &directory, error) &&
             names(directory, &files, error);
        revision->empty = ok && files.count == 0;
        for (size_t j = 0; j < files.count && ok; ++j)
            ok = strcmp(files.items[j].data, "record.json") == 0 ||
                 strcmp(files.items[j].data, "unit.service") == 0 ||
                 strcmp(files.items[j].data, "foreign-unit.json") == 0 ||
                 strcmp(files.items[j].data, "foreign-unit.service") == 0;
        if (ok && !revision->empty)
            ok = budget >= TIRED_SERVICE_RECORD_LIMIT &&
                 tired_file_snapshot(directory, "record.json", TIRED_SERVICE_RECORD_LIMIT,
                                     &revision->metadata, &bytes, error) &&
                 revision->metadata.exists && revision->metadata.uid == geteuid() &&
                 revision->metadata.mode == 0600 &&
                 tired_service_record_parse(bytes.data, bytes.length, &revision->record, error) &&
                 tired_service_record_check_layout(&revision->record, layout, error) &&
                 strcmp(revision->record.metadata.service_uuid, current->metadata.service_uuid) ==
                     0 &&
                 strcmp(revision->record.metadata.revision_uuid, revision->name.data) == 0 &&
                 tired_file_fingerprint(directory, "unit.service", TIRED_PRIVATE_FILE_LIMIT,
                                        &revision->unit, error) &&
                 (!revision->unit.exists ||
                  (revision->unit.uid == geteuid() && revision->unit.mode == 0600 &&
                   strcmp(revision->unit.sha256, revision->record.metadata.unit_sha256) == 0));
        if (ok && !revision->empty)
            ok = foreign_snapshot(directory, revision, &budget, error);
        if (bytes.length <= budget)
            budget -= bytes.length;
        if (ok && revision->unit.exists)
        {
            ok = revision->unit.size <= budget;
            if (ok)
                budget -= (size_t)revision->unit.size;
        }
        tired_text_destroy(&bytes);
        tired_text_list_destroy(&files);
        tired_directory_destroy(directory);
    }
    if (!ok)
        goto done;
    qsort(revisions, count, sizeof(*revisions), latest);
    uint64_t retain = mutation->operation == TIRED_TRANSACTION_REMOVE && !mutation->keep_history
                          ? 0
                          : mutation->history_revisions;
    for (size_t i = 0, kept = 0; i < count; ++i)
    {
        revisions[i].keep =
            !revisions[i].empty &&
            (mutation->operation == TIRED_TRANSACTION_REMOVE && mutation->keep_history
                 ? true
                 : kept < retain);
        kept += revisions[i].keep;
        if (revisions[i].keep && (!revisions[i].unit.exists ||
                                  (revisions[i].has_foreign && !revisions[i].foreign.exists)))
        {
            ok = conflict(error);
            goto done;
        }
    }
    for (size_t i = 0; i < count && ok; ++i)
    {
        Revision *revision = &revisions[i];
        if (revision->keep)
            continue;
        bool referenced =
            mutation->operation != TIRED_TRANSACTION_REMOVE && current->has_environment &&
            strcmp(current->environment_revision, revision->record.environment_revision) == 0;
        for (size_t j = 0; j < count; ++j)
            referenced |= revisions[j].keep && revisions[j].record.has_environment &&
                          strcmp(revisions[j].record.environment_revision,
                                 revision->record.environment_revision) == 0;
        if (!referenced && !environment_remove(layout, &revision->record, error))
        {
            ok = false;
            break;
        }
        TiredDirectory *directory = NULL;
        ok = tired_directory_child(service, revision->name.data, false, true, &directory, error);
        if (ok && !revision->empty)
            ok =
                remove_file(directory, "foreign-unit.service", &revision->foreign, error) &&
                remove_file(directory, "foreign-unit.json", &revision->foreign_descriptor, error) &&
                remove_file(directory, "unit.service", &revision->unit, error) &&
                remove_file(directory, "record.json", &revision->metadata, error);
        if (ok)
            ok = tired_directory_check(directory, error) &&
                 unlinkat(tired_directory_fd(service), revision->name.data, AT_REMOVEDIR) == 0 &&
                 fsync(tired_directory_fd(service)) == 0;
        tired_directory_destroy(directory);
    }
done:
    for (size_t i = 0; revisions != NULL && i < count; ++i)
    {
        tired_text_destroy(&revisions[i].name);
        tired_service_record_destroy(&revisions[i].record);
    }
    free(revisions);
    tired_text_list_destroy(&entries);
    tired_directory_destroy(service);
    tired_directory_destroy(root);
    tired_transaction_inventory_destroy(&transactions);
    return ok || (error->status != TIRED_OK ? false : conflict(error));
}
