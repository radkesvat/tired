#include "tired/encode.h"
#include "tired/file_fingerprint.h"
#include "tired/file_target.h"
#include "tired/service_record.h"
#include <string.h>
#include <unistd.h>
bool tired_service_record_hydrate(TiredServiceRecord *record, const TiredLayout *layout,
                                  TiredError *error)
{
    if (record->environment_loaded)
        return true;
    if (!record->has_environment)
    {
        record->environment_loaded = true;
        return true;
    }
    if (!tired_service_record_check_layout(record, layout, error))
        return false;
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_ENVIRONMENT};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    memcpy(target.revision_uuid, record->environment_revision, 37);
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL;
    TiredFileFingerprint fingerprint = {0};
    TiredText bytes = {0};
    TiredEnvironment parsed = {0}, values = {0};
    TiredBuffer assignment;
    tired_buffer_init(&assignment, TIRED_INPUT_LIMIT);
    bool ok = tired_file_target_resolve(layout, &target, &resolved, error) &&
              tired_directory_open(resolved.directory.data, record->metadata.owner_uid, true,
                                   &directory, error) &&
              tired_file_snapshot(directory, resolved.name.data, TIRED_ENVIRONMENT_FILE_LIMIT,
                                  &fingerprint, &bytes, error);
    if (!ok)
        goto done;
    if (!fingerprint.exists || fingerprint.mode != 0600 ||
        fingerprint.uid != record->metadata.owner_uid ||
        strcmp(fingerprint.sha256, record->environment_sha256) != 0 ||
        !tired_environment_import(&parsed, bytes.data, bytes.length, error) ||
        parsed.count != record->environment.count)
    {
        ok = false;
        goto done;
    }
    for (size_t i = 0; i < record->environment.count; ++i)
    {
        const TiredEnvironmentEntry *metadata = &record->environment.items[i];
        const TiredEnvironmentEntry *value =
            tired_environment_find(&parsed, metadata->name.data, metadata->name.length);
        assignment.length = 0;
        if (value == NULL ||
            !tired_buffer_append(&assignment, metadata->name.data, metadata->name.length, error) ||
            !tired_buffer_append(&assignment, "=", 1, error) ||
            !tired_buffer_append(&assignment, value->value.data, value->value.length, error) ||
            !tired_environment_set(&values, assignment.data, assignment.length, metadata->origin,
                                   metadata->sensitive, error))
        {
            ok = false;
            goto done;
        }
    }
    tired_environment_destroy(&record->environment);
    record->environment = values;
    values = (TiredEnvironment){0};
    record->environment_loaded = true;
done:
    tired_buffer_destroy(&assignment);
    tired_environment_destroy(&parsed);
    tired_environment_destroy(&values);
    tired_text_destroy(&bytes);
    tired_directory_destroy(directory);
    tired_resolved_file_destroy(&resolved);
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_CONFLICT, "environment-integrity",
                        "The immutable environment revision differs from its saved metadata.", 0);
    return ok;
}
