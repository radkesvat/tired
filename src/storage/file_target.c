#include "tired/file_target.h"
#include "tired/encode.h"
#include "tired/io.h"
#include "tired/name.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "file-target",
                           "File target role, identifiers or selectors are invalid.", 0);
}
void tired_resolved_file_destroy(TiredResolvedFile *resolved)
{
    if (resolved == NULL)
        return;
    tired_text_destroy(&resolved->directory);
    tired_text_destroy(&resolved->name);
    *resolved = (TiredResolvedFile){0};
}
bool tired_file_target_validate(const TiredFileTarget *target, TiredError *error)
{
    assert(target != NULL);
    if ((unsigned)target->role > TIRED_FILE_TARGET_RECORD ||
        !tired_uuid_valid(target->service_uuid, strnlen(target->service_uuid, 37)))
        return invalid(error);
    if (target->role == TIRED_FILE_TARGET_ENVIRONMENT)
    {
        if (!tired_uuid_valid(target->revision_uuid, strnlen(target->revision_uuid, 37)))
            return invalid(error);
    }
    else if (target->revision_uuid[0] != '\0')
        return invalid(error);
    if (target->role == TIRED_FILE_TARGET_UNIT)
    {
        const TiredText *name = &target->unit_name;
        if (name->data == NULL || name->length <= 8 ||
            name->length > TIRED_EXPLICIT_NAME_LIMIT + 8 ||
            memcmp(name->data + name->length - 8, ".service", 8) != 0 ||
            !tired_name_validate_base(name->data, name->length - 8, error))
            return invalid(error);
    }
    else if (target->unit_name.data != NULL || target->unit_name.length != 0)
        return invalid(error);
    tired_error_clear(error);
    return true;
}
bool tired_file_target_resolve(const TiredLayout *layout, const TiredFileTarget *target,
                               TiredResolvedFile *output, TiredError *error)
{
    assert(layout != NULL && target != NULL && output != NULL);
    if (!tired_file_target_validate(target, error))
        return false;
    TiredLayoutPath id = target->role == TIRED_FILE_TARGET_UNIT ? TIRED_PATH_UNITS
                         : target->role == TIRED_FILE_TARGET_RECORD
                             ? TIRED_PATH_RECORDS
                             : TIRED_PATH_ENVIRONMENT_SERVICES;
    const TiredText *root = &layout->paths[id];
    if (root->data == NULL || root->length == 0 || root->length > 4096 || root->data[0] != '/')
        return invalid(error);
    TiredResolvedFile resolved = {.mode = target->role == TIRED_FILE_TARGET_UNIT ? 0644 : 0600,
                                  .private_directory = target->role != TIRED_FILE_TARGET_UNIT};
    TiredBuffer directory;
    tired_buffer_init(&directory, 4096);
    bool ok = false;
    if (!tired_buffer_append(&directory, root->data, root->length, error))
        goto done;
    if (target->role == TIRED_FILE_TARGET_ENVIRONMENT)
    {
        if ((root->data[root->length - 1] != '/' &&
             !tired_buffer_append(&directory, "/", 1, error)) ||
            !tired_buffer_append(&directory, target->service_uuid, 36, error) ||
            !tired_buffer_append(&directory, "/revisions/", 11, error) ||
            !tired_buffer_append(&directory, target->revision_uuid, 36, error) ||
            !tired_text_set(&resolved.name, "environment", 11, 255, error))
            goto done;
    }
    else if (target->role == TIRED_FILE_TARGET_RECORD)
    {
        char name[42];
        (void)snprintf(name, sizeof(name), "%s.json", target->service_uuid);
        if (!tired_text_set(&resolved.name, name, 41, 255, error))
            goto done;
    }
    else if (!tired_text_set(&resolved.name, target->unit_name.data, target->unit_name.length, 255,
                             error))
        goto done;
    if (!tired_buffer_take(&directory, &resolved.directory, error))
        goto done;
    size_t separator = resolved.directory.data[resolved.directory.length - 1] == '/' ? 0 : 1;
    if (resolved.directory.length + separator + resolved.name.length >= 4096)
    {
        tired_error_set(error, TIRED_INVALID, "file-target-path-limit",
                        "Resolved file path exceeds the supported filesystem path limit.", 0);
        goto done;
    }
    tired_resolved_file_destroy(output);
    *output = resolved;
    resolved = (TiredResolvedFile){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_resolved_file_destroy(&resolved);
    tired_buffer_destroy(&directory);
    return ok;
}
