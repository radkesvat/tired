#ifndef TIRED_FILE_CHANGE_H
#define TIRED_FILE_CHANGE_H
#include "tired/file_manifest.h"
#include "tired/operation_lock.h"
typedef struct
{
    bool reached, durable;
} TiredFileChangeResult;
/* Apply or reverse one validated manifest entry through a trusted scope layout.
 * Caller binds manifest to approved request/journal, persists intent, establishes
 * ownership, prepares backups/directories, and chooses operation ordering first.
 * All paths/references derive from the manifest. No directory creation or cleanup.
 * Result resets on entry and may report reached=true on post-move failure; false
 * never proves a previous invocation made no changes. Retry reopens stored state.
 * Returned success means this file step, not the entire transaction, completed. */
bool tired_file_change_apply(const TiredLayout *layout, const TiredFileManifest *manifest,
                             size_t index, bool rollback, const TiredOperationLock *lock,
                             TiredFileChangeResult *result, TiredError *error);
#endif
