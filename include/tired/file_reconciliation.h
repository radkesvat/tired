#ifndef TIRED_FILE_RECONCILIATION_H
#define TIRED_FILE_RECONCILIATION_H
#include "tired/file_manifest.h"
typedef enum
{
    TIRED_FILE_UNKNOWN,
    TIRED_FILE_BEFORE,
    TIRED_FILE_AFTER,
    TIRED_FILE_FOREIGN
} TiredFileState;
typedef struct
{
    TiredFileState state;
    TiredFileFingerprint actual;
    TiredError error;
} TiredFileObservation;
typedef struct
{
    TiredFileObservation *files;
    size_t count;
    bool complete, foreign;
} TiredFileReconciliation;
/* Compare destinations in manifest order, using a trusted selected-scope layout.
 * Scope mismatch or invalid manifest fails atomically. Per-file inspection errors
 * remain UNKNOWN alongside successful neighbors. Missing trusted parents mean
 * absence; denied/unsafe parents do not. A 64 MiB content budget bounds reads,
 * plus at most one growth-detection byte per fingerprint attempt.
 * Read-only snapshots, not authorization, artifact verification or an atomic view.
 * Revalidate under the operation lock before any mutation. Output starts zeroed. */
bool tired_file_reconcile(const TiredLayout *layout, const TiredFileManifest *manifest,
                          TiredFileReconciliation *output, TiredError *error);
void tired_file_reconciliation_destroy(TiredFileReconciliation *result);
/* Shared content budget for a sequence of inspections; consumed conservatively
 * as above. Budget is reduced even when individual observations fail. */
bool tired_file_reconcile_budget(const TiredLayout *layout, const TiredFileManifest *manifest,
                                 size_t *remaining, TiredFileReconciliation *output,
                                 TiredError *error);
#endif
