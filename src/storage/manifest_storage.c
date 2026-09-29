#include "tired/manifest_storage.h"
#include "tired/private_file.h"
#include "tired/transaction_journal.h"
#include <assert.h>
#include <string.h>

bool tired_manifest_publish(TiredDirectory *directory, const TiredOperationLock *lock,
                            const TiredFileManifest *manifest, TiredPublication **publication,
                            TiredError *error)
{
    assert(directory != NULL && lock != NULL && manifest != NULL && publication != NULL &&
           *publication == NULL);
    TiredText bytes = {0};
    bool ok = tired_operation_lock_check(lock, error) &&
              tired_file_manifest_encode(manifest, &bytes, error) &&
              tired_publication_prepare(directory, "files.json", bytes.data, bytes.length, 0600,
                                        publication, error) &&
              tired_publication_commit(*publication, lock, error);
    tired_text_destroy(&bytes);
    return ok;
}

bool tired_manifest_read_bound(TiredDirectory *directory, const TiredTransactionRecord *prepared,
                               TiredFileManifest *output, TiredError *error)
{
    assert(directory != NULL && prepared != NULL && output != NULL);
    TiredText bytes = {0}, expected = {0}, actual = {0};
    TiredFileManifest manifest = {0};
    bool ok = false;
    if (!tired_private_file_read(directory, "files.json", TIRED_FILE_MANIFEST_LIMIT, &bytes,
                                 error) ||
        !tired_file_manifest_parse(bytes.data, bytes.length, &manifest, error))
        goto done;
    if (!tired_transaction_record_encode(&manifest.prepared, &expected, error) ||
        !tired_transaction_record_encode(prepared, &actual, error))
        goto done;
    if (expected.length != actual.length ||
        memcmp(expected.data, actual.data, expected.length) != 0)
    {
        tired_error_set(error, TIRED_CONFLICT, "manifest-journal-mismatch",
                        "File manifest does not match its transaction journal.", 0);
        goto done;
    }
    if (!tired_directory_check(directory, error))
        goto done;
    tired_file_manifest_destroy(output);
    *output = manifest;
    manifest = (TiredFileManifest){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_file_manifest_destroy(&manifest);
    tired_text_destroy(&bytes);
    tired_text_destroy(&expected);
    tired_text_destroy(&actual);
    return ok;
}

bool tired_manifest_load(TiredDirectory *directory, TiredFileManifest *output, TiredError *error)
{
    assert(directory != NULL && output != NULL);
    TiredDirectory *journal_directory = NULL;
    TiredTransactionJournal journal = {0};
    bool ok = false;
    if (!tired_directory_child(directory, "journal", false, true, &journal_directory, error) ||
        !tired_transaction_journal_read(journal_directory, &journal, error))
        goto done;
    if (journal.count == 0 || journal.staging_count != 0)
    {
        tired_error_set(error, TIRED_RECOVERY_REQUIRED, "manifest-journal-incomplete",
                        "Manifest requires a prepared journal without unpublished staging.", 0);
        goto done;
    }
    ok = tired_manifest_read_bound(directory, &journal.records[0], output, error);
done:
    tired_directory_destroy(journal_directory);
    tired_transaction_journal_destroy(&journal);
    return ok;
}
