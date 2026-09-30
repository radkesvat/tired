#include "tired/transaction_journal.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool conflict(TiredError *error, const char *message)
{
    return tired_error_set(error, TIRED_CONFLICT, "transaction-journal", message, 0);
}
static bool io_error(TiredError *error)
{
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "transaction-journal-io",
                           "Cannot inspect transaction journal directory.", errno);
}
static bool same_transaction(const TiredTransactionRecord *a, const TiredTransactionRecord *b)
{
    return strcmp(a->transaction_uuid, b->transaction_uuid) == 0 &&
           strcmp(a->service_uuid, b->service_uuid) == 0 &&
           strcmp(a->approved_sha256, b->approved_sha256) == 0 && a->user_scope == b->user_scope &&
           a->operation == b->operation && a->unit_name.length == b->unit_name.length &&
           memcmp(a->unit_name.data, b->unit_name.data, a->unit_name.length) == 0;
}
static bool staging_name(const char *name)
{
    if (strlen(name) != 47 || memcmp(name, ".tired-", 7) != 0 || strcmp(name + 43, ".tmp") != 0)
        return false;
    return tired_uuid_valid(name + 7, 36);
}
void tired_transaction_journal_destroy(TiredTransactionJournal *journal)
{
    if (journal == NULL)
        return;
    for (size_t i = 0; i < journal->count; ++i)
        tired_transaction_record_destroy(&journal->records[i]);
    free(journal->records);
    *journal = (TiredTransactionJournal){0};
}
bool tired_transaction_journal_read(TiredDirectory *directory, TiredTransactionJournal *output,
                                    TiredError *error)
{
    assert(directory != NULL && output != NULL);
    if (!tired_directory_check(directory, error))
        return false;
    int fd = tired_directory_fd(directory);
    struct stat before, after;
    if (fstat(fd, &before) != 0)
        return io_error(error);
    if (before.st_uid != geteuid() || (before.st_mode & 07777) != 0700)
        return conflict(error, "Journal directory must be private and owned by this identity.");
    int scan_fd = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (scan_fd < 0)
        return io_error(error);
    DIR *scan = fdopendir(scan_fd);
    if (scan == NULL)
    {
        io_error(error);
        (void)close(scan_fd);
        return false;
    }
    bool seen[TIRED_TRANSACTION_SEQUENCE_LIMIT + 1] = {0};
    size_t count = 0, maximum = 0;
    TiredTransactionJournal journal = {0};
    TiredText bytes = {0};
    bool ok = false;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (entry == NULL)
        {
            if (errno != 0)
            {
                io_error(error);
                goto done;
            }
            break;
        }
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (staging_name(name))
        {
            if (++journal.staging_count > 128)
            {
                conflict(error, "Journal has too many unpublished staging entries.");
                goto done;
            }
            continue;
        }
        uint64_t sequence;
        if (strlen(name) != 9 || strcmp(name + 4, ".json") != 0 ||
            !tired_parse_u64(name, 4, 1, TIRED_TRANSACTION_SEQUENCE_LIMIT, &sequence, error) ||
            seen[sequence])
        {
            conflict(error, "Journal contains an unexpected or duplicate entry.");
            goto done;
        }
        seen[sequence] = true;
        ++count;
        if (sequence > maximum)
            maximum = (size_t)sequence;
    }
    if (closedir(scan) != 0)
    {
        scan = NULL;
        io_error(error);
        goto done;
    }
    scan = NULL;
    if (maximum != count)
    {
        conflict(error, "Transaction journal has a gap in its record sequence.");
        goto done;
    }
    if (count != 0)
    {
        journal.records = calloc(count, sizeof(*journal.records));
        if (journal.records == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate journal records.",
                            errno);
            goto done;
        }
    }
    journal.count = count;
    for (size_t i = 0; i < count; ++i)
    {
        char name[16];
        (void)snprintf(name, sizeof(name), "%04zu.json", i + 1);
        if (!tired_private_file_read(directory, name, TIRED_TRANSACTION_RECORD_LIMIT, &bytes,
                                     error) ||
            !tired_transaction_record_parse(bytes.data, bytes.length, &journal.records[i], error))
            goto done;
        if (journal.records[i].sequence != i + 1 ||
            (i != 0 && !same_transaction(&journal.records[0], &journal.records[i])))
        {
            conflict(error, "Journal record sequence or transaction identity does not match.");
            goto done;
        }
        if (!tired_transaction_progress_advance(&journal.progress, &journal.records[i], error))
            goto done;
    }
    if (!tired_directory_check(directory, error))
        goto done;
    if (fstat(fd, &after) != 0)
    {
        io_error(error);
        goto done;
    }
    if (before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
    {
        conflict(error, "Journal directory changed while it was being read.");
        goto done;
    }
    tired_transaction_journal_destroy(output);
    *output = journal;
    journal = (TiredTransactionJournal){0};
    tired_error_clear(error);
    ok = true;
done:
    if (scan != NULL)
        (void)closedir(scan);
    tired_text_destroy(&bytes);
    tired_transaction_journal_destroy(&journal);
    return ok;
}
static bool journal_append(TiredDirectory *directory, const TiredOperationLock *lock,
                           const TiredTransactionRecord *record, TiredPublication **publication,
                           TiredError *error)
{
    assert(directory != NULL && lock != NULL && record != NULL &&
           (publication == NULL || *publication == NULL));
    TiredTransactionJournal journal = {0};
    TiredText bytes = {0};
    bool ok = false;
    if (!tired_operation_lock_check(lock, error) ||
        !tired_transaction_record_encode(record, &bytes, error) ||
        !tired_transaction_journal_read(directory, &journal, error))
        goto done;
    if (journal.staging_count != 0)
    {
        tired_error_set(error, TIRED_RECOVERY_REQUIRED, "journal-staging",
                        "Unpublished journal staging requires inspection before appending.", 0);
        goto done;
    }
    if (record->sequence != journal.count + 1 ||
        (journal.count != 0 && !same_transaction(&journal.records[0], record)))
    {
        conflict(error,
                 "Appended record must continue the same transaction without a sequence gap.");
        goto done;
    }
    char name[16];
    if (!tired_transaction_progress_advance(&journal.progress, record, error))
        goto done;
    (void)snprintf(name, sizeof(name), "%04u.json", (unsigned)record->sequence);
    if (publication == NULL)
        ok = tired_private_file_create(directory, name, bytes.data, bytes.length, error);
    else
        ok = tired_publication_prepare(directory, name, bytes.data, bytes.length, 0600, publication,
                                       error) &&
             tired_publication_commit(*publication, lock, error);
done:
    tired_text_destroy(&bytes);
    tired_transaction_journal_destroy(&journal);
    return ok;
}
bool tired_transaction_journal_append(TiredDirectory *directory, const TiredOperationLock *lock,
                                      const TiredTransactionRecord *record,
                                      TiredPublication **publication, TiredError *error)
{
    assert(publication != NULL);
    return journal_append(directory, lock, record, publication, error);
}
bool tired_transaction_journal_append_atomic(TiredDirectory *directory,
                                             const TiredOperationLock *lock,
                                             const TiredTransactionRecord *record,
                                             TiredError *error)
{
    return journal_append(directory, lock, record, NULL, error);
}
