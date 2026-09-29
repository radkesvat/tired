#ifndef TIRED_FILE_TARGET_H
#define TIRED_FILE_TARGET_H
#include "tired/layout.h"
typedef enum
{
    TIRED_FILE_TARGET_UNIT,
    TIRED_FILE_TARGET_ENVIRONMENT,
    TIRED_FILE_TARGET_RECORD
} TiredFileTargetRole;
typedef struct
{
    TiredFileTargetRole role;
    char service_uuid[37], revision_uuid[37];
    TiredText unit_name; /* Borrowed full safe name, required only for UNIT. */
} TiredFileTarget;
typedef struct
{
    TiredText directory, name;
    unsigned mode;
    bool private_directory;
} TiredResolvedFile;
bool tired_file_target_validate(const TiredFileTarget *target, TiredError *error);
/* Pure path derivation from a trusted layout. All roles bind a service UUID;
 * environment targets also require a revision UUID. Reject irrelevant selectors.
 * No caller-supplied destination path/mode. Does not prove managed ownership or
 * authorize writes: controller verifies approval, manifests and existing files.
 * Owned output starts zeroed; failures preserve it. */
bool tired_file_target_resolve(const TiredLayout *layout, const TiredFileTarget *target,
                               TiredResolvedFile *resolved, TiredError *error);
void tired_resolved_file_destroy(TiredResolvedFile *resolved);
#endif
