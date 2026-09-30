#include "tired/plan.h"
#include "tired/encode.h"
#include "tired/io.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tired_plan_destroy(TiredPlan *p)
{
    if (p == NULL)
        return;
    tired_invocation_destroy(&p->invocation);
    tired_account_destroy(&p->invoking);
    tired_account_destroy(&p->service);
    tired_group_destroy(&p->group);
    tired_spec_destroy(&p->spec);
    tired_environment_destroy(&p->environment);
    tired_credentials_destroy(&p->credentials);
    tired_text_destroy(&p->managed_environment);
    tired_profile_destroy(&p->profile);
    tired_text_destroy(&p->profile_path);
    tired_text_list_destroy(&p->profile_candidates);
    free(p->profile_decisions);
    *p = (TiredPlan){0};
}
static bool set_text(TiredServiceSpec *spec, TiredFieldId id, const TiredText *text,
                     TiredFieldOrigin origin, TiredError *error)
{
    return tired_spec_set(spec, id, text->data, text->length, origin, true, error);
}
static bool add_environment(TiredEnvironment *environment, const TiredEnvironmentEntry *entry,
                            TiredError *error)
{
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = tired_buffer_append(&buffer, entry->name.data, entry->name.length, error) &&
              tired_buffer_append(&buffer, "=", 1, error) &&
              tired_buffer_append(&buffer, entry->value.data, entry->value.length, error) &&
              tired_environment_set(environment, buffer.data, buffer.length, entry->origin,
                                    entry->sensitive, error);
    tired_buffer_destroy(&buffer);
    return ok;
}
static bool absolute(const TiredPlan *plan, const TiredText *text, TiredText *output,
                     TiredError *error)
{
    return tired_path_absolute(&plan->invocation.directory, text->data, text->length, output,
                               error);
}

bool tired_plan_prepare(const TiredRequest *request, TiredPlan *output, TiredError *error)
{
    return tired_plan_prepare_settings(request, NULL, output, error);
}

static TiredFieldOrigin configured_origin(TiredSettingsOrigin origin)
{
    assert(origin == TIRED_SETTINGS_ADMIN || origin == TIRED_SETTINGS_USER);
    return origin == TIRED_SETTINGS_ADMIN ? TIRED_ORIGIN_CONFIG_ADMIN : TIRED_ORIGIN_CONFIG_USER;
}

bool tired_plan_configured_defaults(TiredServiceSpec *spec, const TiredSettings *settings,
                                    TiredError *error)
{
    if (settings == NULL)
        return true;
    if (settings->supplied[TIRED_SETTING_RETRY])
    {
        const char *policy = settings->limited_retries ? "limited" : "persistent";
        if (!tired_spec_set(spec, TIRED_FIELD_RETRY_POLICY, policy, strlen(policy),
                            configured_origin(settings->origins[TIRED_SETTING_RETRY]), true, error))
            return false;
    }
    if (settings->supplied[TIRED_SETTING_RESTART_DELAY])
    {
        char delay[32];
        int length = snprintf(delay, sizeof(delay), "%" PRIu64 "us", settings->restart_usec);
        if (!tired_spec_set(spec, TIRED_FIELD_RESTART_SEC, delay, (size_t)length,
                            configured_origin(settings->origins[TIRED_SETTING_RESTART_DELAY]), true,
                            error))
            return false;
    }
    return true;
}

bool tired_plan_service_defaults(const TiredServiceSpec *spec, const TiredSettings *settings,
                                 TiredServiceSpec *output, TiredError *error)
{
    TiredServiceSpec defaults = {0};
    TiredBuffer description;
    tired_buffer_init(&description, TIRED_INPUT_LIMIT);
    bool ok = tired_spec_defaults(&defaults, error) &&
              tired_plan_configured_defaults(&defaults, settings, error);
    if (ok && tired_field_has_value(&spec->fields[TIRED_FIELD_NAME]))
    {
        const TiredText *name = &spec->fields[TIRED_FIELD_NAME].value.text;
        ok = tired_spec_set(&defaults, TIRED_FIELD_SYSLOG_IDENTIFIER, name->data, name->length,
                            TIRED_ORIGIN_CAPTURE, true, error) &&
             tired_buffer_append(&description, name->data, name->length, error) &&
             tired_buffer_append(&description, " (managed by tired)",
                                 sizeof(" (managed by tired)") - 1, error) &&
             tired_spec_set(&defaults, TIRED_FIELD_DESCRIPTION, description.data,
                            description.length, TIRED_ORIGIN_DEFAULT, true, error);
    }
    if (ok)
    {
        tired_spec_destroy(output);
        *output = defaults;
        defaults = (TiredServiceSpec){0};
    }
    tired_buffer_destroy(&description);
    tired_spec_destroy(&defaults);
    return ok;
}

bool tired_plan_prepare_settings(const TiredRequest *request, const TiredSettings *settings,
                                 TiredPlan *output, TiredError *error)
{
    assert(request != NULL && output != NULL);
    TiredPlan plan = {0};
    memcpy(plan.sensitive_arguments, request->sensitive_arguments,
           sizeof(plan.sensitive_arguments));
    TiredText path = {0}, contents = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    if (!tired_invocation_capture(&request->arguments, getenv("PATH"), &plan.invocation, error) ||
        !tired_proposal_generic(&plan.invocation, user, &plan.spec, &plan.invoking,
                                &plan.name_basis, error))
        goto fail;
    if (!user && plan.invocation.uid == 0)
    {
        if (!tired_invoking_account(false, &plan.invoking, error) ||
            !set_text(&plan.spec, TIRED_FIELD_RUN_AS, &plan.invoking.name, TIRED_ORIGIN_CAPTURE,
                      error) ||
            !set_text(&plan.spec, TIRED_FIELD_GROUP, &plan.invoking.primary_group.name,
                      TIRED_ORIGIN_CAPTURE, error))
            goto fail;
        plan.invocation.uid = plan.invoking.uid;
        plan.invocation.gid = plan.invoking.primary_group.gid;
    }
    if (!tired_plan_configured_defaults(&plan.spec, settings, error))
        goto fail;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (request->overrides.fields[i].origin == TIRED_ORIGIN_USER &&
            !tired_spec_copy_field(&plan.spec, &request->overrides, (TiredFieldId)i, error))
            goto fail;
    if (request->seen[TIRED_FIELD_NAME])
    {
        const TiredText *name = &plan.spec.fields[TIRED_FIELD_NAME].value.text;
        if (!request->seen[TIRED_FIELD_SYSLOG_IDENTIFIER] &&
            !set_text(&plan.spec, TIRED_FIELD_SYSLOG_IDENTIFIER, name, TIRED_ORIGIN_DEFAULT, error))
            goto fail;
        if (!request->seen[TIRED_FIELD_DESCRIPTION])
        {
            if (!tired_buffer_append(&buffer, name->data, name->length, error) ||
                !tired_buffer_append(&buffer, " (managed by tired)",
                                     sizeof(" (managed by tired)") - 1, error) ||
                !tired_spec_set(&plan.spec, TIRED_FIELD_DESCRIPTION, buffer.data, buffer.length,
                                TIRED_ORIGIN_DEFAULT, true, error))
                goto fail;
            buffer.length = 0;
        }
    }
    if (request->working_directory.data != NULL &&
        (!absolute(&plan, &request->working_directory, &path, error) ||
         !set_text(&plan.spec, TIRED_FIELD_WORKING_DIRECTORY, &path, TIRED_ORIGIN_USER, error)))
        goto fail;
    const TiredText *run_as = &plan.spec.fields[TIRED_FIELD_RUN_AS].value.text;
    if (!tired_account_resolve(run_as->data, run_as->length, &plan.service, error))
        goto fail;
    if (request->seen[TIRED_FIELD_GROUP])
    {
        const TiredText *group = &plan.spec.fields[TIRED_FIELD_GROUP].value.text;
        if (!tired_group_resolve(group->data, group->length, &plan.group, error))
            goto fail;
    }
    else if (!tired_group_by_gid(plan.service.primary_group.gid, &plan.group, error))
        goto fail;
    if (user &&
        (plan.service.uid != plan.invoking.uid || request->seen[TIRED_FIELD_GROUP] ||
         request->overrides.fields[TIRED_FIELD_SUPPLEMENTARY_GROUPS].origin == TIRED_ORIGIN_USER))
    {
        tired_error_set(error, TIRED_INVALID, "user-identity",
                        "User scope cannot switch execution identity or groups.", 0);
        goto fail;
    }
    if (!set_text(&plan.spec, TIRED_FIELD_RUN_AS, &plan.service.name,
                  request->seen[TIRED_FIELD_RUN_AS] ? TIRED_ORIGIN_USER : TIRED_ORIGIN_CAPTURE,
                  error) ||
        !set_text(&plan.spec, TIRED_FIELD_GROUP, &plan.group.name,
                  request->seen[TIRED_FIELD_GROUP] ? TIRED_ORIGIN_USER : TIRED_ORIGIN_CAPTURE,
                  error))
        goto fail;
    for (size_t i = 0; i < request->import_files.count; ++i)
    {
        if (!absolute(&plan, &request->import_files.items[i], &path, error) ||
            !tired_read_file(path.data, TIRED_ENVIRONMENT_FILE_LIMIT, &contents, error) ||
            !tired_environment_import(&plan.environment, contents.data, contents.length, error))
            goto fail;
    }
    for (size_t i = 0; i < request->pass_environment.count; ++i)
        if (!tired_environment_pass(&plan.environment, request->pass_environment.items[i].data,
                                    request->pass_environment.items[i].length, error))
            goto fail;
    for (size_t i = 0; i < request->environment.count; ++i)
        if (!add_environment(&plan.environment, &request->environment.items[i], error))
            goto fail;
    for (size_t i = 0; i < request->environment_files.count; ++i)
    {
        if (!absolute(&plan, &request->environment_files.items[i], &path, error) ||
            !tired_spec_append(&plan.spec, TIRED_FIELD_ENVIRONMENT_FILES, path.data, path.length,
                               TIRED_ORIGIN_USER, error))
            goto fail;
    }
    for (size_t i = 0; i < request->credentials.count; ++i)
    {
        const TiredText *item = &request->credentials.items[i];
        const char *equal = memchr(item->data, '=', item->length);
        if (equal == NULL || equal + 1 == item->data + item->length)
        {
            tired_error_set(error, TIRED_INVALID, "credential-reference",
                            "Expected credential NAME=PATH.", 0);
            goto fail;
        }
        if (!tired_path_absolute(&plan.invocation.directory, equal + 1,
                                 item->length - (size_t)(equal + 1 - item->data), &path, error))
            goto fail;
        buffer.length = 0;
        if (!tired_buffer_append(&buffer, item->data, (size_t)(equal + 1 - item->data), error) ||
            !tired_buffer_append(&buffer, path.data, path.length, error) ||
            !tired_credentials_add(&plan.credentials, buffer.data, buffer.length, error))
            goto fail;
    }
    if (!tired_spec_resolve_scope(&plan.spec, error) ||
        !tired_spec_resolve_retry(&plan.spec, error) || !tired_uuid_create(plan.uuid, error))
        goto fail;
    if (plan.environment.count != 0)
    {
        buffer.length = 0;
        if (user)
        {
            const char *xdg = getenv("XDG_CONFIG_HOME");
            if (xdg != NULL && xdg[0] != '\0')
            {
                size_t size = strnlen(xdg, TIRED_INPUT_LIMIT + 1);
                if (xdg[0] != '/' || size > TIRED_INPUT_LIMIT ||
                    !tired_validate_text(xdg, size, true, error))
                {
                    tired_error_set(error, TIRED_INVALID, "xdg-path",
                                    "User configuration location must be a valid absolute path.",
                                    0);
                    goto fail;
                }
                if (!tired_buffer_append(&buffer, xdg, size, error))
                    goto fail;
            }
            else if (!tired_buffer_append(&buffer, plan.invoking.home.data,
                                          plan.invoking.home.length, error) ||
                     !tired_buffer_append(&buffer, "/.config", 8, error))
                goto fail;
            if (!tired_buffer_append(&buffer, "/tired/services/", 16, error))
                goto fail;
        }
        else if (!tired_buffer_append(&buffer, "/etc/tired/services/", 20, error))
            goto fail;
        if (!tired_buffer_append(&buffer, plan.uuid, 36, error) ||
            !tired_buffer_append(&buffer, "/revisions/1/environment",
                                 sizeof("/revisions/1/environment") - 1, error) ||
            !tired_buffer_take(&buffer, &plan.managed_environment, error))
            goto fail;
    }
    tired_text_destroy(&path);
    tired_text_destroy(&contents);
    tired_buffer_destroy(&buffer);
    tired_plan_destroy(output);
    *output = plan;
    tired_error_clear(error);
    return true;
fail:
    tired_text_destroy(&path);
    tired_text_destroy(&contents);
    tired_buffer_destroy(&buffer);
    tired_plan_destroy(&plan);
    return false;
}

bool tired_plan_apply_profiles(TiredPlan *plan, const TiredProfileCatalog *catalog,
                               const char *selection, const TiredProfileContext *context,
                               TiredError *error)
{
    assert(plan != NULL && catalog != NULL && context != NULL);
    if (plan->profile_matching)
        return tired_error_set(error, TIRED_CONFLICT, "profile-reselection",
                               "Rebuild the input proposal before changing profile selection.", 0);
    if (selection != NULL && strcmp(selection, "none") == 0)
        return true;
    const TiredProfileEntry *selected = NULL;
    size_t count = 0;
    bool user = tired_spec_choice_is(&plan->spec, TIRED_FIELD_SCOPE, "user");
    if (!tired_catalog_select(catalog, &plan->invocation.executable, selection, user, &selected,
                              &count, error))
        return false;
    TiredProfile snapshot = {0};
    TiredProfileMerge merge = {0};
    TiredText path = {0};
    TiredTextList candidates = {0};
    if (count > 1)
    {
        for (size_t i = 0; i < catalog->count; ++i)
        {
            const TiredProfileEntry *entry = &catalog->items[i];
            if (!tired_catalog_entry_active(catalog, i, user) ||
                strcmp(entry->profile.id, "generic") == 0)
                continue;
            if (tired_profile_matches(&entry->profile, &plan->invocation.executable) &&
                !tired_text_list_append(&candidates, entry->profile.id, strlen(entry->profile.id),
                                        256, 65536, error))
                goto fail;
        }
    }
    if (selected != NULL)
    {
        const char *json =
            json_object_to_json_string_ext(selected->profile.document, JSON_C_TO_STRING_PLAIN);
        if (json == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation",
                            "Cannot snapshot selected profile.", 0);
            goto fail;
        }
        if (!tired_profile_parse(json, strlen(json), &snapshot, error) ||
            !tired_text_set(&path, selected->path.data, selected->path.length, TIRED_INPUT_LIMIT,
                            error) ||
            !tired_profile_merge(&snapshot, &plan->spec, context, &merge, error))
            goto fail;
    }
    tired_profile_destroy(&plan->profile);
    plan->profile = snapshot;
    tired_text_destroy(&plan->profile_path);
    plan->profile_path = path;
    tired_text_list_destroy(&plan->profile_candidates);
    plan->profile_candidates = candidates;
    free(plan->profile_decisions);
    plan->profile_decisions = merge.decisions;
    merge.decisions = NULL;
    plan->profile_matching = true;
    plan->profile_explicit = selection != NULL && strcmp(selection, "auto") != 0;
    if (selected != NULL)
    {
        memcpy(plan->profile_digest, selected->digest, sizeof(plan->profile_digest));
        plan->profile_origin = selected->origin;
        tired_spec_destroy(&plan->spec);
        plan->spec = merge.spec;
        merge.spec = (TiredServiceSpec){0};
    }
    tired_profile_merge_destroy(&merge);
    tired_error_clear(error);
    return true;
fail:
    tired_profile_destroy(&snapshot);
    tired_profile_merge_destroy(&merge);
    tired_text_destroy(&path);
    tired_text_list_destroy(&candidates);
    return false;
}
