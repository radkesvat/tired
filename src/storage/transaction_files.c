#include "tired/transaction_files.h"
#include "tired/manifest_storage.h"
#include "tired/transaction_journal.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static bool append(TiredDirectory *directory, const TiredOperationLock *lock,
                   const TiredTransactionRecord *record, TiredError *error)
{
    TiredPublication *publication = NULL;
    bool ok = tired_transaction_journal_append(directory, lock, record, &publication, error);
    /* Preserve journal staging and published entries on every outcome. */
    tired_publication_destroy(publication);
    return ok;
}
bool tired_transaction_files_apply(const TiredLayout *layout, TiredDirectory *transaction,
                                   const TiredTransactionRecord *approved, bool rollback,
                                   const TiredOperationLock *lock,
                                   TiredTransactionFilesResult *result, TiredError *error)
{
    assert(layout != NULL && transaction != NULL && approved != NULL && lock != NULL &&
           result != NULL);
    *result = (TiredTransactionFilesResult){.phase.failed_index = SIZE_MAX};
    TiredFileManifest manifest = {0};
    TiredTransactionJournal journal = {0};
    TiredDirectory *directory = NULL;
    TiredText expected = {0}, actual = {0};
    bool ok = false;
    if (!tired_operation_lock_check(lock, error) ||
        !tired_manifest_read_bound(transaction, approved, &manifest, error) ||
        !tired_directory_child(transaction, "journal", false, true, &directory, error) ||
        !tired_transaction_journal_read(directory, &journal, error))
        goto done;
    TiredTransactionAction action =
        approved->operation == TIRED_TRANSACTION_REMOVE   ? TIRED_ACTION_REMOVE_FILES
        : approved->operation > TIRED_TRANSACTION_RESTORE ? TIRED_ACTION_STORE_RECORD
                                                          : TIRED_ACTION_PUBLISH_FILES;
    if (journal.count == 0 || journal.staging_count != 0 ||
        layout->user_scope != approved->user_scope ||
        journal.progress.mode != (rollback ? TIRED_PROGRESS_ROLLBACK : TIRED_PROGRESS_FORWARD) ||
        (journal.progress.pending && journal.progress.pending_action != action) ||
        journal.count > TIRED_TRANSACTION_SEQUENCE_LIMIT - (journal.progress.pending ? 1U : 2U))
    {
        tired_error_set(error, TIRED_RECOVERY_REQUIRED, "transaction-files-state",
                        "Journal cannot admit or resume this file phase.", 0);
        goto done;
    }
    if (!tired_transaction_record_encode(approved, &expected, error) ||
        !tired_transaction_record_encode(&journal.records[0], &actual, error))
        goto done;
    if (expected.length != actual.length ||
        memcmp(expected.data, actual.data, expected.length) != 0)
    {
        tired_error_set(error, TIRED_CONFLICT, "transaction-files-identity",
                        "Approved file transaction does not match the journal.", 0);
        goto done;
    }
    TiredTransactionRecord record = *approved;
    record.action = action;
    record.sequence = journal.count + 1;
    if (!journal.progress.pending)
    {
        record.state = TIRED_ACTION_INTENT;
        if (!append(directory, lock, &record, error))
            goto done;
        ++record.sequence;
    }
    /* Published journal records have synced bytes; a prior directory sync may
     * have failed after rename. Reestablish namespace durability before replay. */
    if (journal.progress.pending && fsync(tired_directory_fd(directory)) != 0)
    {
        tired_error_set(error, TIRED_RUNTIME_FAILED, "transaction-files-intent-sync",
                        "Cannot make the resumed file intent durable.", errno);
        goto done;
    }
    result->intent_durable = true;
    result->files_completed = tired_file_phase_apply(layout, &manifest, rollback, lock,
                                                     &result->phase, &result->file_error);
    record.state = result->files_completed ? TIRED_ACTION_COMPLETED : TIRED_ACTION_UNCERTAIN;
    if (!append(directory, lock, &record, error))
        goto done;
    result->outcome_durable = true;
    if (!result->files_completed)
    {
        if (error != NULL)
            *error = result->file_error;
        goto done;
    }
    tired_error_clear(error);
    ok = true;
done:
    tired_text_destroy(&expected);
    tired_text_destroy(&actual);
    tired_directory_destroy(directory);
    tired_transaction_journal_destroy(&journal);
    tired_file_manifest_destroy(&manifest);
    return ok;
}
