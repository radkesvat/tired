#ifndef TIRED_SERVICE_FILES_H
#define TIRED_SERVICE_FILES_H
#include "tired/file_fingerprint.h"
#include "tired/service_record.h"
typedef enum
{
    TIRED_SERVICE_FILE_UNKNOWN,
    TIRED_SERVICE_FILE_NOT_REQUIRED,
    TIRED_SERVICE_FILE_MISSING,
    TIRED_SERVICE_FILE_DRIFTED,
    TIRED_SERVICE_FILE_MATCH
} TiredServiceFileState;
typedef struct
{
    TiredServiceFileState state;
    TiredFileFingerprint actual;
    /* Meaningful only for an observed existing file. Marker applies to unit only. */
    bool owner_matches, mode_matches, digest_matches, marker_matches;
    TiredError error;
} TiredServiceFile;
typedef struct
{
    TiredServiceFile unit, environment;
} TiredServiceFiles;
/* Inspect destinations derived from layout for a validated service record. Unit
 * reads are bounded to the renderer's 4 MiB limit, owned environment to 16 MiB.
 * No retained file bytes.
 * Scope/record path disagreement fails atomically; per-file failures are UNKNOWN
 * with diagnostics so a readable neighbor remains useful. Missing is distinct.
 * MATCH is disk evidence only: current manager fragment/drop-ins, record freshness
 * and operation approval must also be checked before any mutation. No writes. */
bool tired_service_files_inspect(const TiredLayout *layout, const TiredServiceRecord *record,
                                 TiredServiceFiles *output, TiredError *error);
/* Multi-service variant. Shared budget charges successful observed bytes and a
 * failed read's bounded allowance. Exhaustion yields UNKNOWN with a diagnostic. */
bool tired_service_files_inspect_budget(const TiredLayout *layout, const TiredServiceRecord *record,
                                        size_t *remaining, TiredServiceFiles *output,
                                        TiredError *error);
#endif
