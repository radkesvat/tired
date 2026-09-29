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
typedef struct
{
    size_t indices[TIRED_FILE_CHANGE_LIMIT], count;
} TiredFileOrder;
/* Stable dependency order independent of input order. Add environment revisions,
 * add/replace units, remove old units, then update records. Removal deletes units,
 * records, then environment revisions. Rollback is the exact reverse. */
bool tired_file_change_order(const TiredFileManifest *manifest, bool rollback,
                             TiredFileOrder *order, TiredError *error);
typedef struct
{
    size_t completed, failed_index; /* SIZE_MAX when no file step failed. */
    bool reached, durable;          /* Progress of the failed step, if any. */
} TiredFilePhaseResult;
/* Apply in dependency order, stopping at the first error and retaining artifacts.
 * Caller persists phase intent before entry and outcome afterward, and revalidates
 * all preconditions under the lock. No automatic rollback or manager operations.
 * completed counts durable steps this invocation; retry starts from the beginning
 * and each primitive recognizes already-applied state. */
bool tired_file_phase_apply(const TiredLayout *layout, const TiredFileManifest *manifest,
                            bool rollback, const TiredOperationLock *lock,
                            TiredFilePhaseResult *result, TiredError *error);
#endif
