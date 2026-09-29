#include "tired/service_record_storage.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static bool identifier(const char *uuid, TiredError *error)
{
    return tired_uuid_valid(uuid, strnlen(uuid, 37)) ||
           tired_error_set(error, TIRED_INVALID, "service-record-id",
                           "Expected a canonical service UUID.", 0);
}
bool tired_service_record_read_budget(TiredDirectory *directory, const TiredLayout *layout,
                                      const char *uuid, size_t *budget, TiredServiceRecord *output,
                                      TiredError *error)
{
    assert(directory != NULL && layout != NULL && uuid != NULL && budget != NULL && output != NULL);
    if (!identifier(uuid, error))
        return false;
    char name[42];
    (void)snprintf(name, sizeof(name), "%s.json", uuid);
    TiredText bytes = {0};
    TiredServiceRecord record = {0};
    bool ok = false;
    if (*budget == 0)
        return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "service-record-budget",
                               "Service record read budget exhausted.", 0);
    size_t limit = *budget < TIRED_SERVICE_RECORD_LIMIT ? *budget : TIRED_SERVICE_RECORD_LIMIT;
    if (!tired_private_file_read(directory, name, limit, &bytes, error))
    {
        *budget -= limit;
        goto done;
    }
    *budget -= bytes.length;
    if (!tired_service_record_parse(bytes.data, bytes.length, &record, error))
        goto done;
    uid_t owner = layout->user_scope ? geteuid() : 0;
    if (strcmp(record.metadata.service_uuid, uuid) != 0 ||
        record.metadata.user_scope != layout->user_scope || record.metadata.owner_uid != owner)
    {
        tired_error_set(error, TIRED_CONFLICT, "service-record-identity",
                        "Service record identity does not match its filename, scope or owner.", 0);
        goto done;
    }
    if (!tired_service_record_check_layout(&record, layout, error) ||
        !tired_directory_check(directory, error))
        goto done;
    tired_service_record_destroy(output);
    *output = record;
    record = (TiredServiceRecord){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_service_record_destroy(&record);
    tired_text_destroy(&bytes);
    return ok;
}
bool tired_service_record_read(TiredDirectory *directory, const TiredLayout *layout,
                               const char *uuid, TiredServiceRecord *output, TiredError *error)
{
    size_t budget = TIRED_SERVICE_RECORD_LIMIT;
    return tired_service_record_read_budget(directory, layout, uuid, &budget, output, error);
}
bool tired_service_record_load(const TiredLayout *layout, const char *uuid,
                               TiredServiceRecord *output, TiredError *error)
{
    assert(layout != NULL && uuid != NULL && output != NULL);
    if (!identifier(uuid, error))
        return false;
    const TiredText *path = &layout->paths[TIRED_PATH_RECORDS];
    if (path->data == NULL)
        return tired_error_set(error, TIRED_INVALID, "service-record-layout",
                               "Records layout is unset.", 0);
    TiredDirectory *directory = NULL;
    bool ok = tired_directory_open(path->data, layout->user_scope ? geteuid() : 0, true, &directory,
                                   error) &&
              tired_service_record_read(directory, layout, uuid, output, error);
    tired_directory_destroy(directory);
    return ok;
}
