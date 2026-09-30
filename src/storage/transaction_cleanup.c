#define _GNU_SOURCE
#include "tired/backup_ledger.h"
#include "tired/file_fingerprint.h"
#include "tired/history.h"
#include "tired/io.h"
#include "tired/manifest_storage.h"
#include "tired/private_file.h"
#include "tired/transaction_inventory.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static bool failure(TiredError *error)
{
    return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "transaction-cleanup-conflict",
                           "Committed service state is preserved. A transaction artifact is "
                           "changed or unknown; cleanup stopped.",
                           errno);
}
static bool known_file(const char *name)
{
    size_t length = strlen(name);
    if (length == 18 && memcmp(name, "effect-", 7) == 0 && name[11] == '-' &&
        (name[12] == '1' || name[12] == '2') && strcmp(name + 13, ".json") == 0)
    {
        for (size_t i = 7; i < 11; ++i)
            if (name[i] < '0' || name[i] > '9')
                return false;
        return true;
    }
    if (strcmp(name, "request.json") == 0 || strcmp(name, "before.json") == 0 ||
        strcmp(name, "files.json") == 0 || strcmp(name, "receipt.json") == 0 ||
        strcmp(name, "cleanup.json") == 0 || strcmp(name, "cleanup.pending") == 0)
        return true;
    for (size_t i = 1; i <= 5; ++i)
    {
        char allowed[32];
        (void)snprintf(allowed, sizeof(allowed), "stage-%zu.json", i);
        if (strcmp(name, allowed) == 0)
            return true;
        (void)snprintf(allowed, sizeof(allowed), "backup-%zu.json", i);
        if (strcmp(name, allowed) == 0)
            return true;
    }
    return false;
}
static bool directory_names(TiredDirectory *directory, TiredTextList *names, TiredError *error)
{
    int fd =
        openat(tired_directory_fd(directory), ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *scan = fd < 0 ? NULL : fdopendir(fd);
    if (scan == NULL)
    {
        if (fd >= 0)
            close(fd);
        return failure(error);
    }
    bool ok = true;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (entry == NULL)
        {
            ok = errno == 0;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!tired_text_list_append(names, entry->d_name, strlen(entry->d_name), 4096,
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
static bool clean_one(TiredDirectory *transaction, const TiredTransactionJournal *journal,
                      bool keep_receipt, const TiredMutation *mutation, struct json_object *rows,
                      TiredError *error)
{
    TiredFileManifest manifest = {0};
    TiredFileFingerprint exists = {0};
    TiredDirectory *artifacts = NULL;
    TiredTextList entries = {0}, files = {0};
    bool ok = tired_file_fingerprint(transaction, "files.json", TIRED_FILE_MANIFEST_LIMIT, &exists,
                                     error);
    if (ok && exists.exists)
        ok = tired_manifest_read_bound(transaction, &journal->records[0], &manifest, error);
    if (!ok || !tired_directory_child(transaction, "artifacts", false, true, &artifacts, error) ||
        !directory_names(artifacts, &files, error) ||
        !directory_names(transaction, &entries, error))
    {
        ok = false;
        goto done;
    }
    for (size_t i = 0; i < entries.count; ++i)
        if (!known_file(entries.items[i].data) && strcmp(entries.items[i].data, "journal") != 0 &&
            strcmp(entries.items[i].data, "artifacts") != 0)
        {
            ok = failure(error);
            goto done;
        }
    for (size_t i = 0; i < files.count; ++i)
    {
        const char *digest = NULL;
        char declared_digest[65] = {0};
        for (size_t j = 0; j < manifest.count; ++j)
            if (manifest.files[j].before.exists &&
                strcmp(files.items[i].data, manifest.files[j].rollback_uuid) == 0)
                digest = manifest.files[j].before.sha256;
        for (size_t j = 1; digest == NULL && j <= 5; ++j)
        {
            bool found = false;
            char uuid[37] = {0};
            TiredFileFingerprint before = {0};
            if (!tired_backup_ledger_read(transaction, j, &found, uuid, &before, error))
            {
                ok = false;
                goto done;
            }
            if (found && strcmp(files.items[i].data, uuid) == 0)
            {
                memcpy(declared_digest, before.sha256, 65);
                digest = declared_digest;
            }
        }
        if (digest == NULL)
        {
            ok = failure(error);
            goto done;
        }
        TiredFileFingerprint actual = {0};
        if (!tired_file_fingerprint(artifacts, files.items[i].data, TIRED_PRIVATE_FILE_LIMIT,
                                    &actual, error) ||
            !actual.exists || actual.uid != geteuid() || actual.mode != 0600 ||
            strcmp(actual.sha256, digest) != 0)
        {
            ok = failure(error);
            goto done;
        }
    }
    if (keep_receipt)
    {
        char receipt[1024];
        int n = snprintf(
            receipt, sizeof(receipt),
            "{\"schema_version\":1,\"service_uuid\":\"%s\",\"transaction_uuid\":\"%s\","
            "\"unit_name\":\"%s\",\"scope\":\"%s\",\"outcome\":\"removed\",\"updated_usec\":%llu}"
            "\n",
            mutation->proposed.metadata.service_uuid, mutation->proposed.metadata.transaction_uuid,
            mutation->proposed.metadata.unit_name.data,
            mutation->proposed.metadata.user_scope ? "user" : "system",
            (unsigned long long)mutation->proposed.metadata.updated_usec);
        TiredText prior = {0};
        ok = n > 0 && (size_t)n < sizeof(receipt);
        if (ok &&
            !tired_private_file_create(transaction, "receipt.json", receipt, (size_t)n, error))
        {
            ok = error->status == TIRED_CONFLICT &&
                 tired_private_file_read(transaction, "receipt.json", sizeof(receipt), &prior,
                                         error) &&
                 prior.length == (size_t)n && memcmp(prior.data, receipt, prior.length) == 0;
        }
        tired_text_destroy(&prior);
        if (!ok)
            goto done;
    }
    for (size_t i = 0; i < files.count && ok; ++i)
        ok = tired_cleanup_record(rows, journal->records[0].transaction_uuid, "artifacts",
                                  files.items[i].data, artifacts, false, error);
    for (size_t i = 0; i < entries.count && ok; ++i)
        if (known_file(entries.items[i].data) &&
            !(keep_receipt && (strcmp(entries.items[i].data, "receipt.json") == 0 ||
                               strcmp(entries.items[i].data, "cleanup.pending") == 0 ||
                               strcmp(entries.items[i].data, "cleanup.json") == 0)))
            ok = tired_cleanup_record(rows, journal->records[0].transaction_uuid, "",
                                      entries.items[i].data, transaction, false, error);
done:
    tired_directory_destroy(artifacts);
    tired_text_list_destroy(&entries);
    tired_text_list_destroy(&files);
    tired_file_manifest_destroy(&manifest);
    return ok;
}
bool tired_transaction_retire(const TiredLayout *layout, const TiredMutation *mutation,
                              const TiredBackend *backend, TiredError *error)
{
    TiredTransactionInventory inventory = {0};
    TiredDirectory *root = NULL, *current_directory = NULL, *current_journal = NULL;
    TiredTransactionJournal current_history = {0};
    struct json_object *rows = json_object_new_array();
    TiredFileFingerprint ledger = {0};
    bool ok =
        rows != NULL &&
        tired_directory_open(layout->paths[TIRED_PATH_TRANSACTIONS].data, geteuid(), true, &root,
                             error) &&
        tired_directory_child(root, mutation->proposed.metadata.transaction_uuid, false, true,
                              &current_directory, error) &&
        tired_directory_child(current_directory, "journal", false, true, &current_journal, error) &&
        tired_transaction_journal_read(current_journal, &current_history, error) &&
        current_history.count != 0 &&
        tired_file_fingerprint(current_directory, "cleanup.json", TIRED_PRIVATE_FILE_LIMIT, &ledger,
                               error);
    if (!ok)
        goto done;
    if (ledger.exists)
    {
        ok = tired_transaction_cleanup_resume(layout, current_directory,
                                              &current_history.records[0], backend, error);
        goto done;
    }
    ok = tired_transaction_inventory_load(layout, &inventory, error) && inventory.complete;
    for (size_t i = 0; ok && i < inventory.count; ++i)
    {
        const TiredTransactionInventoryEntry *entry = &inventory.entries[i];
        bool current =
            strcmp(entry->directory_name.data, mutation->proposed.metadata.transaction_uuid) == 0;
        if (!current &&
            (entry->cleanup_pending || (entry->progress.mode != TIRED_PROGRESS_COMMITTED &&
                                        entry->progress.mode != TIRED_PROGRESS_ROLLED_BACK)))
        {
            ok = failure(error);
            break;
        }
        if (current && (mutation->operation != TIRED_TRANSACTION_REMOVE || mutation->keep_history))
            continue;
        TiredDirectory *transaction = NULL, *directory = NULL, *artifacts = NULL;
        TiredTransactionJournal journal = {0};
        ok = tired_directory_child(root, entry->directory_name.data, false, true, &transaction,
                                   error) &&
             tired_directory_child(transaction, "journal", false, true, &directory, error) &&
             tired_transaction_journal_read(directory, &journal, error) && journal.count != 0;
        bool related = ok && strcmp(journal.records[0].service_uuid,
                                    mutation->proposed.metadata.service_uuid) == 0;
        if (related)
            ok = (journal.progress.mode == TIRED_PROGRESS_COMMITTED ||
                  journal.progress.mode == TIRED_PROGRESS_ROLLED_BACK) &&
                 clean_one(transaction, &journal, current, mutation, rows, error);
        if (related && ok && !current)
        {
            for (size_t j = 0; j < journal.count && ok; ++j)
            {
                char name[32];
                (void)snprintf(name, sizeof(name), "%04zu.json", j + 1);
                ok = tired_cleanup_record(rows, entry->directory_name.data, "journal", name,
                                          directory, false, error);
            }
            if (ok)
                ok = tired_directory_child(transaction, "artifacts", false, true, &artifacts,
                                           error) &&
                     tired_cleanup_record(rows, entry->directory_name.data, "artifacts", "",
                                          artifacts, true, error) &&
                     tired_cleanup_record(rows, entry->directory_name.data, "journal", "",
                                          directory, true, error) &&
                     tired_cleanup_record(rows, entry->directory_name.data, "", "", transaction,
                                          true, error);
        }
        tired_transaction_journal_destroy(&journal);
        tired_directory_destroy(directory);
        tired_directory_destroy(artifacts);
        tired_directory_destroy(transaction);
    }
    if (ok)
        ok = tired_cleanup_save(current_directory, &current_history.records[0], rows, error);
    if (ok)
    {
        if (backend != NULL && backend->tick != NULL)
            backend->tick(backend->context, "cleanup_prepared");
        ok = tired_transaction_cleanup_resume(layout, current_directory,
                                              &current_history.records[0], backend, error);
    }
done:
    json_object_put(rows);
    tired_transaction_journal_destroy(&current_history);
    tired_directory_destroy(current_journal);
    tired_directory_destroy(current_directory);
    tired_directory_destroy(root);
    tired_transaction_inventory_destroy(&inventory);
    return ok || (error->status != TIRED_OK ? false : failure(error));
}
