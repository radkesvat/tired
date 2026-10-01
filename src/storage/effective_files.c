#include "tired/effective_files.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include "tired/memory.h"
#include "tired/unit_redaction.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
void tired_effective_files_destroy(TiredEffectiveFiles *files)
{
    if (files == NULL)
        return;
    for (size_t i = 0; i < files->count; ++i)
    {
        tired_text_destroy(&files->files[i].reported_path);
        tired_observed_file_destroy(&files->files[i].source);
        if (files->files[i].display.data != NULL)
            tired_memory_clear(files->files[i].display.data, files->files[i].display.length);
        tired_text_destroy(&files->files[i].display);
    }
    free(files->files);
    *files = (TiredEffectiveFiles){0};
}
bool tired_effective_files_collect(const TiredServiceRecord *record,
                                   const TiredUnitQueryResult *query, bool include_sensitive,
                                   TiredEffectiveFiles *output, TiredError *error)
{
    assert(record != NULL && query != NULL && output != NULL);
    TiredEffectiveFiles result = {.complete = true};
    if (!query->done || query->error.status != TIRED_OK)
    {
        result.error = query->error;
        if (result.error.status == TIRED_OK)
            tired_error_set(&result.error, TIRED_RECOVERY_REQUIRED, "effective-query-incomplete",
                            "Configuration observation is incomplete.", 0);
        result.complete = false;
        goto success;
    }
    if (query->unit_name == NULL || strcmp(query->unit_name, record->metadata.unit_name.data) != 0)
        return tired_error_set(error, TIRED_CONFLICT, "effective-query-name",
                               "Configuration observation names a different unit.", 0);
    if (!query->object_found || query->observation == NULL)
    {
        tired_error_set(&result.error, TIRED_NOT_FOUND, "effective-unit-missing",
                        "No manager unit configuration is available.", 0);
        result.complete = false;
        goto success;
    }
    const TiredObservedValue *fragment = &query->observation->fields[TIRED_OBS_FRAGMENT_PATH];
    const TiredObservedValue *dropins = &query->observation->fields[TIRED_OBS_DROP_IN_PATHS];
    if (dropins->known && dropins->value.list.count > 256)
        return tired_error_set(error, TIRED_INVALID, "effective-file-count",
                               "Too many reported drop-in paths.", 0);
    if (!dropins->known)
    {
        tired_error_set(&result.error, TIRED_RECOVERY_REQUIRED, "effective-dropins-unknown",
                        "Manager drop-in paths are unknown.", 0);
        result.complete = false;
    }
    size_t count = 1 + (dropins->known ? dropins->value.list.count : 0);
    result.files = calloc(count, sizeof(*result.files));
    if (result.files == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate effective-file observations.", 0);
    result.count = count;
    size_t budget = 16U * TIRED_INPUT_LIMIT;
    for (size_t i = 0; i < count; ++i)
    {
        TiredEffectiveFile *file = &result.files[i];
        const TiredText *path = i == 0 ? (fragment->known ? &fragment->value.text : NULL)
                                       : &dropins->value.list.items[i - 1];
        if (path == NULL || path->length == 0)
        {
            tired_error_set(&file->error, path == NULL ? TIRED_RECOVERY_REQUIRED : TIRED_NOT_FOUND,
                            "effective-fragment-unavailable",
                            "Manager fragment path is unavailable.", 0);
            result.complete = false;
            continue;
        }
        if (!tired_text_set(&file->reported_path, path->data, path->length, 4096, error))
            goto failed;
        if (!tired_observed_file_read(path->data, record->metadata.user_scope, &budget,
                                      &file->source, &file->error))
            goto incomplete;
        if (!file->source.fingerprint.exists)
        {
            tired_error_set(&file->error, TIRED_NOT_FOUND, "effective-file-missing",
                            "Reported configuration file is missing.", 0);
            goto incomplete;
        }
        if (!tired_validate_text(file->source.bytes.data, file->source.bytes.length, false,
                                 &file->error))
            goto incomplete;
        if (include_sensitive)
        {
            if (!tired_text_set(&file->display, file->source.bytes.data, file->source.bytes.length,
                                TIRED_UNIT_LIMIT, &file->error))
                goto incomplete;
        }
        else if (!tired_unit_redact(file->source.bytes.data, file->source.bytes.length,
                                    &record->spec.fields[TIRED_FIELD_ARGV].value.list,
                                    record->review.sensitive_arguments, &record->environment,
                                    &file->display, &file->redaction, &file->error))
            goto incomplete;
        continue;
    incomplete:
        result.complete = false;
    }
success:
    tired_effective_files_destroy(output);
    *output = result;
    tired_error_clear(error);
    return true;
failed:
    tired_effective_files_destroy(&result);
    return false;
}
