#ifndef TIRED_PUBLICATION_H
#define TIRED_PUBLICATION_H
#include "tired/file_fingerprint.h"
#include "tired/operation_lock.h"
typedef struct TiredPublication TiredPublication;
/* Reopen an interrupted publication under the scope lock using journal-bound
 * before/after fingerprints and the original staging UUID. Accept only prepared
 * staging, published creation, or exchanged replacement with retained before-state.
 * No rename/cleanup occurs; durability remains false until commit is retried.
 * Failure preserves a null output. Does not establish transaction authorization. */
bool tired_publication_reopen(TiredDirectory *directory, const char *name, const char *staging_uuid,
                              const TiredFileFingerprint *before, const TiredFileFingerprint *after,
                              const TiredOperationLock *lock, TiredPublication **publication,
                              TiredError *error);
/* Stage <=16 MiB in the destination directory under a random exclusive name.
 * mode is 0600 for private data or 0644 for nonsecret unit bytes. Directory must
 * outlive the handle. On failure a handle may be returned: discard, then destroy.
 * Does not publish the requested name or replace any existing entry. */
bool tired_publication_prepare(TiredDirectory *directory, const char *name, const char *data,
                               size_t length, unsigned mode, TiredPublication **publication,
                               TiredError *error);
/* Check the held scope lock and staged identity, then RENAME_NOREPLACE and fsync
 * the directory. No fallback if the filesystem lacks the required atomic rename.
 * After rename succeeds, published remains true even if directory sync fails.
 * Retrying then verifies the published binding and retries sync, never renames.
 * Controller is responsible for matching the lock/directory to the selected scope. */
bool tired_publication_commit(TiredPublication *publication, const TiredOperationLock *lock,
                              TiredError *error);
/* Replace an existing expected file with RENAME_EXCHANGE, retaining its inode at
 * temporary_name for recovery. Check expected fingerprint before and after the
 * exchange; never automatically erase/restore the displaced entry. Foreign writers
 * can race the check: a post-exchange conflict leaves published=true and requires
 * recovery inspection. Retry validates both bindings and syncs, never exchanges
 * again. Expected fingerprint cannot change after exchange. Caller must journal
 * intent and retain verified rollback material before invoking this operation. */
bool tired_publication_replace(TiredPublication *publication, const TiredOperationLock *lock,
                               const TiredFileFingerprint *expected, TiredError *error);
bool tired_publication_published(const TiredPublication *publication);
bool tired_publication_durable(const TiredPublication *publication);
const char *tired_publication_temporary_name(const TiredPublication *publication);
/* Remove only an unpublished staged inode after checking its binding. Retryable.
 * Never removes a published destination. Syncs directory after staging removal. */
bool tired_publication_discard(TiredPublication *publication, TiredError *error);
/* Releases descriptors/memory only; report retained staging on discard failure. */
void tired_publication_destroy(TiredPublication *publication);
#endif
