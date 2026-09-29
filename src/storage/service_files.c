#include "tired/service_files.h"
#include "tired/encode.h"
#include "tired/file_target.h"
#include "tired/private_file.h"
#include <assert.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void inspect(const TiredLayout *layout, const TiredFileTarget *target, const char *digest,
                    size_t *remaining, TiredServiceFile *file)
{
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL;
    TiredText bytes = {0};
    uid_t owner = layout->user_scope ? geteuid() : 0;
    bool unit = target->role == TIRED_FILE_TARGET_UNIT;
    if (!tired_file_target_resolve(layout, target, &resolved, &file->error))
        goto done;
    if (!tired_directory_open(resolved.directory.data, owner, resolved.private_directory,
                              &directory, &file->error))
    {
        if (file->error.status == TIRED_NOT_FOUND)
        {
            file->state = TIRED_SERVICE_FILE_MISSING;
            tired_error_clear(&file->error);
        }
        goto done;
    }
    if (*remaining == 0)
    {
        tired_error_set(&file->error, TIRED_RECOVERY_REQUIRED, "service-files-budget",
                        "Service file inspection byte budget exhausted.", 0);
        goto done;
    }
    size_t limit = unit ? TIRED_UNIT_LIMIT : TIRED_PRIVATE_FILE_LIMIT;
    if (*remaining < limit)
        limit = *remaining;
    bool known = unit ? tired_file_snapshot(directory, resolved.name.data, limit, &file->actual,
                                            &bytes, &file->error)
                      : tired_file_fingerprint(directory, resolved.name.data, limit, &file->actual,
                                               &file->error);
    *remaining -= known ? (size_t)file->actual.size : limit;
    if (!known)
        goto done;
    if (!file->actual.exists)
    {
        file->state = TIRED_SERVICE_FILE_MISSING;
        goto done;
    }
    file->owner_matches = file->actual.uid == owner;
    file->mode_matches = file->actual.mode == resolved.mode;
    file->digest_matches = strcmp(file->actual.sha256, digest) == 0;
    if (unit)
    {
        char marker[80];
        int length = snprintf(marker, sizeof(marker), "# Managed by tired; id=%s; schema=1\n",
                              target->service_uuid);
        assert(length > 0 && (size_t)length < sizeof(marker));
        file->marker_matches =
            bytes.length >= (size_t)length && memcmp(bytes.data, marker, (size_t)length) == 0;
    }
    file->state = file->owner_matches && file->mode_matches && file->digest_matches &&
                          (!unit || file->marker_matches)
                      ? TIRED_SERVICE_FILE_MATCH
                      : TIRED_SERVICE_FILE_DRIFTED;
done:
    if (bytes.data != NULL)
        OPENSSL_cleanse(bytes.data, bytes.length);
    tired_text_destroy(&bytes);
    tired_directory_destroy(directory);
    tired_resolved_file_destroy(&resolved);
}

bool tired_service_files_inspect_budget(const TiredLayout *layout, const TiredServiceRecord *record,
                                        size_t *remaining, TiredServiceFiles *output,
                                        TiredError *error)
{
    assert(layout != NULL && record != NULL && remaining != NULL && output != NULL);
    if (!tired_service_record_check_layout(record, layout, error))
        return false;
    if (record->metadata.owner_uid != (layout->user_scope ? geteuid() : 0))
        return tired_error_set(error, TIRED_CONFLICT, "service-files-owner",
                               "Record owner differs from the selected scope owner.", 0);
    TiredServiceFiles result = {.environment.state = TIRED_SERVICE_FILE_NOT_REQUIRED};
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_UNIT,
                              .unit_name = record->metadata.unit_name};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    inspect(layout, &target, record->metadata.unit_sha256, remaining, &result.unit);
    if (record->has_environment)
    {
        target.role = TIRED_FILE_TARGET_ENVIRONMENT;
        target.unit_name = (TiredText){0};
        memcpy(target.revision_uuid, record->environment_revision, 37);
        result.environment = (TiredServiceFile){0};
        inspect(layout, &target, record->environment_sha256, remaining, &result.environment);
    }
    *output = result;
    tired_error_clear(error);
    return true;
}
bool tired_service_files_inspect(const TiredLayout *layout, const TiredServiceRecord *record,
                                 TiredServiceFiles *output, TiredError *error)
{
    size_t remaining = TIRED_UNIT_LIMIT + TIRED_PRIVATE_FILE_LIMIT;
    return tired_service_files_inspect_budget(layout, record, &remaining, output, error);
}
