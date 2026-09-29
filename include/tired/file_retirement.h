#ifndef TIRED_FILE_RETIREMENT_H
#define TIRED_FILE_RETIREMENT_H
#include "tired/file_fingerprint.h"
#include "tired/operation_lock.h"
typedef struct
{
    bool moved, durable;
} TiredFileRetirement;
/* Remove a managed destination by RENAME_NOREPLACE to .tired-UUID.removed in the
 * same directory. Retain that inode for recovery; never unlink or overwrite it.
 * Verify expected before-state and held scope lock. Retry accepts only an absent
 * destination and matching retained inode, and syncs without moving again.
 * Output is reset and reports observed progress even on failure. A racing foreign
 * writer may be moved: post-move mismatch leaves moved=true and preserves it.
 * Caller must persist intent/UUID and rollback material, then inspect failures.
 * This is not service ownership authorization or eventual artifact cleanup. */
bool tired_file_retire(TiredDirectory *directory, const char *name, const char *uuid,
                       const TiredFileFingerprint *expected, const TiredOperationLock *lock,
                       TiredFileRetirement *result, TiredError *error);
/* Inverse move from the retained name to an absent destination. Never replaces
 * an existing entry. Retry recognizes the expected restored inode and absent
 * retained name. Result.moved means restored/recognized, with the same failure
 * semantics as retirement. Caller must authorize and journal rollback first. */
bool tired_file_unretire(TiredDirectory *directory, const char *name, const char *uuid,
                         const TiredFileFingerprint *expected, const TiredOperationLock *lock,
                         TiredFileRetirement *result, TiredError *error);
#endif
