#include "tired/file_artifact.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool tired_file_artifact_observe(const TiredLayout *layout, TiredDirectory *transaction,
                                 const TiredFileChange *change, bool rollback, size_t limit,
                                 TiredArtifactObservation *output, TiredError *error)
{
    assert(layout != NULL && transaction != NULL && change != NULL && output != NULL);
    TiredError local_error = {0};
    if (error == NULL)
        error = &local_error;
    const char *uuid = rollback ? change->rollback_uuid : change->staging_uuid;
    const TiredFileFingerprint *expected = rollback ? &change->before : &change->after;
    if (!tired_uuid_valid(uuid, strnlen(uuid, 37)) || !expected->exists ||
        limit > TIRED_PRIVATE_FILE_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "artifact-input",
                               "Artifact requires a valid reference and existing expected file.",
                               0);
    TiredText encoded = {0};
    bool valid = tired_file_fingerprint_encode(expected, &encoded, error) &&
                 tired_file_target_validate(&change->target, error);
    tired_text_destroy(&encoded);
    if (!valid || !tired_directory_check(transaction, error))
        return false;
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL;
    TiredArtifactObservation result = {0};
    char staging[48];
    const char *name = uuid;
    bool ok = false, opened;
    if (rollback)
        opened = tired_directory_child(transaction, "artifacts", false, true, &directory, error);
    else
    {
        if (!tired_file_target_resolve(layout, &change->target, &resolved, error))
            goto done;
        (void)snprintf(staging, sizeof(staging), ".tired-%s.tmp", uuid);
        name = staging;
        opened = tired_directory_open(resolved.directory.data, layout->user_scope ? geteuid() : 0,
                                      resolved.private_directory, &directory, error);
    }
    if (!opened)
    {
        if (error->status != TIRED_NOT_FOUND)
            goto done;
    }
    else if (!tired_file_fingerprint(directory, name, limit, &result.actual, error))
        goto done;
    if (!result.actual.exists)
        result.state = TIRED_ARTIFACT_MISSING;
    else if (rollback)
        result.state = result.actual.uid == geteuid() && result.actual.mode == 0600 &&
                               result.actual.size == expected->size &&
                               strcmp(result.actual.sha256, expected->sha256) == 0
                           ? TIRED_ARTIFACT_MATCH
                           : TIRED_ARTIFACT_DIFFERENT;
    else
        result.state = tired_file_fingerprint_equal(&result.actual, expected)
                           ? TIRED_ARTIFACT_MATCH
                           : TIRED_ARTIFACT_DIFFERENT;
    *output = result;
    tired_error_clear(error);
    ok = true;
done:
    tired_directory_destroy(directory);
    tired_resolved_file_destroy(&resolved);
    return ok;
}
