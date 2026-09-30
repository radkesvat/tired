#include "tired/capture.h"
#include "tired/config_frontend.h"
#include "tired/encode.h"
#include "tired/frontend.h"
#include "tired/io.h"
#include "tired/profile_frontend.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
bool tired_edit_inputs(const TiredRequest *request, TiredMutation *mutation,
                       const TiredBackend *backend, const TiredSettings *settings,
                       const char *bundled_directory, TiredError *error)
{
    TiredServiceRecord *record = &mutation->proposed;
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL)
        return tired_error_set(error, TIRED_INVALID, "edit-directory",
                               "Cannot capture edit input directory.", 0);
    TiredText directory = {.data = cwd, .length = strlen(cwd)}, path = {0}, bytes = {0};
    TiredBuffer assignment;
    tired_buffer_init(&assignment, TIRED_INPUT_LIMIT);
    TiredProfileCatalog catalog = {0};
    TiredPlan plan = {0};
    TiredServiceSpec defaults = {0};
    TiredEnvironment additions = {0};
    bool ok = false;
    if (request->replacement.count != 0)
    {
        tired_mutation_change(mutation, TIRED_FIELD_ARGV);
        TiredInvocation invocation = {0};
        bool captured =
            tired_invocation_capture(&request->replacement, getenv("PATH"), &invocation, error) &&
            tired_spec_clear_list(&record->spec, TIRED_FIELD_ARGV, TIRED_ORIGIN_USER, error) &&
            tired_spec_set(&record->spec, TIRED_FIELD_EXECUTABLE, invocation.executable.data,
                           invocation.executable.length, TIRED_ORIGIN_USER, true, error);
        for (size_t i = 0; captured && i < invocation.argv.count; ++i)
            captured =
                tired_spec_append(&record->spec, TIRED_FIELD_ARGV, invocation.argv.items[i].data,
                                  invocation.argv.items[i].length, TIRED_ORIGIN_USER, error);
        tired_invocation_destroy(&invocation);
        if (!captured)
            goto done;
        tired_text_list_destroy(&record->external_config_paths);
        memset(record->review.sensitive_arguments, 0, sizeof(record->review.sensitive_arguments));
        memcpy(record->review.sensitive_arguments, request->sensitive_arguments,
               sizeof(request->sensitive_arguments));
    }
    for (size_t i = 0; i < request->import_files.count; ++i)
        if (!tired_path_absolute(&directory, request->import_files.items[i].data,
                                 request->import_files.items[i].length, &path, error) ||
            !tired_read_file(path.data, TIRED_ENVIRONMENT_FILE_LIMIT, &bytes, error) ||
            !tired_environment_import(&additions, bytes.data, bytes.length, error))
            goto done;
    for (size_t i = 0; i < request->pass_environment.count; ++i)
        if (!tired_environment_pass(&additions, request->pass_environment.items[i].data,
                                    request->pass_environment.items[i].length, error))
            goto done;
    for (size_t i = 0; i < request->environment.count; ++i)
    {
        const TiredEnvironmentEntry *entry = &request->environment.items[i];
        assignment.length = 0;
        if (!tired_buffer_append(&assignment, entry->name.data, entry->name.length, error) ||
            !tired_buffer_append(&assignment, "=", 1, error) ||
            !tired_buffer_append(&assignment, entry->value.data, entry->value.length, error) ||
            !tired_environment_set(&additions, assignment.data, assignment.length,
                                   TIRED_ENV_EXPLICIT, entry->sensitive, error))
            goto done;
    }
    /* Precedence applies within this edit. A newly selected snapshot replaces a
     * saved value regardless of how that historical value was originally set. */
    for (size_t i = 0; i < additions.count; ++i)
    {
        const TiredEnvironmentEntry *entry = &additions.items[i];
        assignment.length = 0;
        if (!tired_buffer_append(&assignment, entry->name.data, entry->name.length, error) ||
            !tired_buffer_append(&assignment, "=", 1, error) ||
            !tired_buffer_append(&assignment, entry->value.data, entry->value.length, error) ||
            !tired_environment_set(&record->environment, assignment.data, assignment.length,
                                   TIRED_ENV_EDITED, entry->sensitive, error))
            goto done;
    }
    for (size_t i = 1; i < TIRED_ARGUMENT_LIMIT; ++i)
        if (request->sensitive_arguments[i])
        {
            record->review.acknowledged[TIRED_RISK_SENSITIVE_COMMAND] = false;
            if (i >= record->spec.fields[TIRED_FIELD_ARGV].value.list.count)
            {
                tired_error_set(error, TIRED_INVALID, "sensitive-argument-index",
                                "Sensitive index exceeds the saved command argument count.", 0);
                goto done;
            }
            record->review.sensitive_arguments[i] = true;
        }
    for (size_t i = 0; i < request->environment_files.count; ++i)
        if (!tired_path_absolute(&directory, request->environment_files.items[i].data,
                                 request->environment_files.items[i].length, &path, error) ||
            !tired_spec_append(&record->spec, TIRED_FIELD_ENVIRONMENT_FILES, path.data, path.length,
                               TIRED_ORIGIN_USER, error))
            goto done;
    for (size_t i = 0; i < request->credentials.count; ++i)
    {
        const TiredText *entry = &request->credentials.items[i];
        const char *equals = memchr(entry->data, '=', entry->length);
        if (equals == NULL)
            goto done;
        if (!tired_path_absolute(&directory, equals + 1,
                                 entry->length - (size_t)(equals + 1 - entry->data), &path, error))
            goto done;
        assignment.length = 0;
        if (!tired_buffer_append(&assignment, entry->data, (size_t)(equals + 1 - entry->data),
                                 error) ||
            !tired_buffer_append(&assignment, path.data, path.length, error) ||
            !tired_credentials_add(&record->credentials, assignment.data, assignment.length, error))
            goto done;
    }
    record->environment_loaded = true;
    if (request->refresh_profile || request->profile.data != NULL)
    {
        memset(record->review.acknowledged, 0, sizeof(record->review.acknowledged));
        if (!tired_plan_service_defaults(&record->spec, settings, &defaults, error))
            goto done;
        for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
            if (record->spec.fields[i].origin == TIRED_ORIGIN_PROFILE &&
                !tired_spec_copy_field(&record->spec, &defaults, (TiredFieldId)i, error))
                goto done;
        if (request->profile.data != NULL && strcmp(request->profile.data, "none") == 0)
        {
            tired_profile_snapshot_destroy(&record->profile);
            record->has_profile = false;
        }
        else
        {
            if (!tired_spec_encode(&record->spec, &bytes, error) ||
                !tired_spec_parse(bytes.data, bytes.length, &plan.spec, error) ||
                !tired_text_set(&plan.invocation.executable, record->executable.lexical_path.data,
                                record->executable.lexical_path.length, TIRED_INPUT_LIMIT, error) ||
                !tired_profiles_discover_settings(bundled_directory, record->metadata.user_scope,
                                                  settings, &catalog, error))
                goto done;
            const char *selection = request->profile.data != NULL ? request->profile.data
                                    : record->has_profile         ? record->profile.profile.id
                                                                  : "auto";
            if (!tired_plan_apply_profiles(&plan, &catalog, selection, &backend->features, error))
                goto done;
            tired_spec_destroy(&record->spec);
            record->spec = plan.spec;
            plan.spec = (TiredServiceSpec){0};
            if (plan.profile.document != NULL)
            {
                TiredProfileSnapshot snapshot = {.profile = plan.profile,
                                                 .source_path = plan.profile_path,
                                                 .source_origin = plan.profile_origin,
                                                 .explicit_selection = plan.profile_explicit,
                                                 .decisions = plan.profile_decisions,
                                                 .count = plan.profile.count};
                memcpy(snapshot.source_sha256, plan.profile_digest, 65);
                if (!tired_profile_snapshot_encode(&snapshot, &bytes, error) ||
                    !tired_profile_snapshot_parse(bytes.data, bytes.length, &record->profile,
                                                  error))
                    goto done;
                record->has_profile = true;
            }
            else
            {
                tired_profile_snapshot_destroy(&record->profile);
                record->has_profile = false;
            }
        }
    }
    ok = true;
done:
    free(cwd);
    tired_text_destroy(&path);
    tired_text_destroy(&bytes);
    tired_buffer_destroy(&assignment);
    tired_catalog_destroy(&catalog);
    tired_plan_destroy(&plan);
    tired_spec_destroy(&defaults);
    tired_environment_destroy(&additions);
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_INVALID, "edit-input", "Invalid explicit edit input.", 0);
    return ok;
}
