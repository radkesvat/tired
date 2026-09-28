#include "tired/plan.h"
#include "tired/encode.h"
#include "tired/io.h"
#include <assert.h>
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
    assert(request != NULL && output != NULL);
    TiredPlan plan = {0};
    TiredText path = {0}, contents = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    if (!tired_invocation_capture(&request->arguments, getenv("PATH"), &plan.invocation, error) ||
        !tired_proposal_generic(&plan.invocation, user, &plan.spec, &plan.invoking,
                                &plan.name_basis, error))
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
        !tired_spec_resolve_retry(&plan.spec, error) ||
        !tired_spec_validate_scalars(&plan.spec, error) || !tired_uuid_create(plan.uuid, error))
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
