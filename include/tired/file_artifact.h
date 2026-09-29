#ifndef TIRED_FILE_ARTIFACT_H
#define TIRED_FILE_ARTIFACT_H
#include "tired/file_manifest.h"
typedef enum
{
    TIRED_ARTIFACT_UNKNOWN,
    TIRED_ARTIFACT_MATCH,
    TIRED_ARTIFACT_MISSING,
    TIRED_ARTIFACT_DIFFERENT
} TiredArtifactState;
typedef struct
{
    TiredArtifactState state;
    TiredFileFingerprint actual;
} TiredArtifactObservation;
/* Read one artifact, bounded to limit <=16 MiB. Staging names are derived from
 * staging_uuid in the resolved destination directory and require exact after
 * identity/metadata/content. Rollback names are rollback_uuid in transaction's
 * private artifacts/ child and require owner 0600 plus before size/content;
 * a backup copy naturally has a different inode and may have a different mode.
 * The caller supplies a validated manifest change, selected scope layout and
 * trusted transaction directory. No mutation or authorization. Atomic output.
 * Missing is a successful observation, not permission to continue recovery. */
bool tired_file_artifact_observe(const TiredLayout *layout, TiredDirectory *transaction,
                                 const TiredFileChange *change, bool rollback, size_t limit,
                                 TiredArtifactObservation *output, TiredError *error);
/* Inspect the retained original inode: replacement uses staging_uuid.tmp;
 * removal uses rollback_uuid.removed. Requires an existing before-state and
 * exact before identity/metadata/content, unlike a copied rollback artifact. */
bool tired_file_retained_observe(const TiredLayout *layout, TiredDirectory *transaction,
                                 const TiredFileChange *change, size_t limit,
                                 TiredArtifactObservation *output, TiredError *error);
#endif
