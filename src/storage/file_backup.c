#include "tired/file_backup.h"
#include "tired/io.h"
#include <assert.h>
#include <openssl/crypto.h>
#include <string.h>

static bool matches(const TiredFileFingerprint *expected, const TiredFileFingerprint *actual,
                    TiredError *error)
{
    return tired_file_fingerprint_equal(expected, actual) ||
           tired_error_set(error, TIRED_CONFLICT, "backup-source-changed",
                           "Rollback source differs from its recorded fingerprint.", 0);
}
bool tired_file_backup(TiredDirectory *source, const char *name,
                       const TiredFileFingerprint *expected, TiredDirectory *artifacts,
                       const char *uuid, const TiredOperationLock *lock,
                       TiredPublication **publication, TiredError *error)
{
    assert(source != NULL && name != NULL && expected != NULL && artifacts != NULL &&
           uuid != NULL && lock != NULL && publication != NULL && *publication == NULL);
    if (!expected->exists || !tired_uuid_valid(uuid, strnlen(uuid, 37)))
        return tired_error_set(error, TIRED_INVALID, "backup-input",
                               "Rollback backup requires an existing source and canonical UUID.",
                               0);
    TiredText bytes = {0}, encoded = {0};
    TiredFileFingerprint actual = {0};
    bool ok = tired_operation_lock_check(lock, error) &&
              tired_file_fingerprint_encode(expected, &encoded, error) &&
              tired_file_snapshot(source, name, (size_t)expected->size, &actual, &bytes, error) &&
              matches(expected, &actual, error) &&
              tired_publication_prepare(artifacts, uuid, bytes.data, bytes.length, 0600,
                                        publication, error) &&
              tired_file_fingerprint(source, name, (size_t)expected->size, &actual, error) &&
              matches(expected, &actual, error) &&
              tired_publication_commit(*publication, lock, error);
    if (bytes.data != NULL)
        OPENSSL_cleanse(bytes.data, bytes.length);
    tired_text_destroy(&bytes);
    tired_text_destroy(&encoded);
    return ok;
}
