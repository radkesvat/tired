#include "tired/frontend.h"
#include "tired/collision.h"
#include "tired/config_frontend.h"
#include "tired/encode.h"
#include "tired/file_target.h"
#include "tired/helper.h"
#include "tired/io.h"
#include "tired/list_frontend.h"
#include "tired/name.h"
#include "tired/plan_output.h"
#include "tired/private_file.h"
#include "tired/profile_frontend.h"
#include "tired/render.h"
#include "tired/service_files.h"
#include "tired/service_inventory.h"
#include "tired/service_record_storage.h"
#include "tired/ui.h"
#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static bool text(TiredText *output, const char *bytes, TiredError *error)
{
    return tired_text_set(output, bytes, strlen(bytes), TIRED_INPUT_LIMIT, error);
}
static bool derive(TiredServiceRecord *record, const TiredLayout *layout, TiredFileTargetRole role,
                   TiredText *output, TiredError *error)
{
    TiredFileTarget target = {.role = role};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    if (role == TIRED_FILE_TARGET_UNIT)
        target.unit_name = record->metadata.unit_name;
    if (role == TIRED_FILE_TARGET_ENVIRONMENT)
        memcpy(target.revision_uuid, record->environment_revision, 37);
    TiredResolvedFile resolved = {0};
    bool ok = tired_file_target_resolve(layout, &target, &resolved, error) &&
              tired_path_absolute(&resolved.directory, resolved.name.data, resolved.name.length,
                                  output, error);
    tired_resolved_file_destroy(&resolved);
    return ok;
}
static bool capture_record(TiredServiceRecord *record, TiredInvocation *invocation,
                           TiredError *error)
{
    TiredTextList argv = record->spec.fields[TIRED_FIELD_ARGV].value.list;
    if (argv.count == 0)
        return false;
    TiredText *items = calloc(argv.count, sizeof(*items));
    if (items == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot capture command facts.",
                               0);
    memcpy(items, argv.items, argv.count * sizeof(*items));
    items[0] = record->spec.fields[TIRED_FIELD_EXECUTABLE].value.text;
    argv.items = items;
    bool ok = tired_invocation_capture(&argv, NULL, invocation, error) &&
              tired_executable_evidence_capture(invocation, &record->executable, error);
    free(items);
    return ok;
}
static bool configuration_references(TiredServiceRecord *record, TiredError *error)
{
    tired_text_list_destroy(&record->external_config_paths);
    if (!record->has_profile || (strcmp(record->profile.profile.id, "backhaul") != 0 &&
                                 strcmp(record->profile.profile.id, "frpc") != 0 &&
                                 strcmp(record->profile.profile.id, "frps") != 0))
        return true;
    const TiredTextList *argv = &record->spec.fields[TIRED_FIELD_ARGV].value.list;
    const TiredText *directory = &record->spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text;
    for (size_t i = 1; i < argv->count; ++i)
    {
        const TiredText *argument = &argv->items[i];
        const char *selected = NULL;
        if (strcmp(argument->data, "-c") == 0 || strcmp(argument->data, "--config") == 0)
        {
            if (++i == argv->count)
                return tired_error_set(error, TIRED_INVALID, "config-argument",
                                       "The selected config option requires a path argument.", 0);
            selected = argv->items[i].data;
        }
        else if (strncmp(argument->data, "--config=", 9) == 0)
            selected = argument->data + 9;
        if (selected != NULL)
        {
            TiredText absolute = {0};
            bool ok =
                tired_path_absolute(directory, selected, strlen(selected), &absolute, error) &&
                tired_text_list_append(&record->external_config_paths, absolute.data,
                                       absolute.length, 256, TIRED_INPUT_LIMIT, error);
            tired_text_destroy(&absolute);
            if (!ok)
                return false;
        }
    }
    return true;
}
bool tired_mutation_refresh(TiredMutation *mutation, const TiredLayout *layout, TiredError *error)
{
    TiredServiceRecord *record = &mutation->proposed;
    TiredServiceSpec *spec = &record->spec;
    TiredText unit = {0}, environment = {0};
    TiredAccount account = {0};
    TiredGroup group = {0};
    TiredInvocation invocation = {0};
    bool ok = false;
    const TiredText *base = &spec->fields[TIRED_FIELD_NAME].value.text;
    TiredBuffer name;
    tired_buffer_init(&name, 255);
    bool named = tired_buffer_append(&name, base->data, base->length, error) &&
                 tired_buffer_append(&name, ".service", 8, error) &&
                 tired_buffer_take(&name, &record->metadata.unit_name, error);
    tired_buffer_destroy(&name);
    if (named && !spec->fields[TIRED_FIELD_SYSLOG_IDENTIFIER].inherit &&
        (spec->fields[TIRED_FIELD_SYSLOG_IDENTIFIER].origin == TIRED_ORIGIN_CAPTURE ||
         spec->fields[TIRED_FIELD_SYSLOG_IDENTIFIER].origin == TIRED_ORIGIN_DEFAULT) &&
        !tired_spec_set(spec, TIRED_FIELD_SYSLOG_IDENTIFIER, base->data, base->length,
                        TIRED_ORIGIN_CAPTURE, true, error))
        goto done;
    if (!named || !tired_spec_resolve_scope(spec, error) ||
        !tired_spec_resolve_retry(spec, error) || !tired_spec_validate_scalars(spec, error))
        goto done;
    if (record->metadata.user_scope != layout->user_scope ||
        record->metadata.user_scope != tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "user"))
    {
        tired_error_set(error, TIRED_CONFLICT, "scope-changed",
                        "Changing scope requires a fresh captured proposal.", 0);
        goto done;
    }
    if (mutation->operation <= TIRED_TRANSACTION_RESTORE &&
        mutation->operation != TIRED_TRANSACTION_REMOVE)
    {
        const TiredText *selector = &spec->fields[TIRED_FIELD_RUN_AS].value.text;
        const TiredText *group_selector = &spec->fields[TIRED_FIELD_GROUP].value.text;
        if (!tired_account_resolve(selector->data, selector->length, &account, error))
            goto done;
        if (account.uid != record->metadata.service_uid &&
            spec->fields[TIRED_FIELD_GROUP].origin == TIRED_ORIGIN_CAPTURE &&
            !tired_spec_set(spec, TIRED_FIELD_GROUP, account.primary_group.name.data,
                            account.primary_group.name.length, TIRED_ORIGIN_CAPTURE, true, error))
            goto done;
        if (!tired_group_resolve(group_selector->data, group_selector->length, &group, error))
            goto done;
        record->metadata.service_uid = account.uid;
        record->metadata.service_gid = group.gid;
        if (!capture_record(record, &invocation, error) || !configuration_references(record, error))
            goto done;
        record->review.argument_count = spec->fields[TIRED_FIELD_ARGV].value.list.count;
        for (size_t i = record->review.argument_count; i < TIRED_ARGUMENT_LIMIT; ++i)
            record->review.sensitive_arguments[i] = false;
        if (!derive(record, layout, TIRED_FILE_TARGET_UNIT, &record->unit_path, error))
            goto done;
        record->has_environment = record->environment.count != 0;
        if (record->has_environment)
        {
            memcpy(record->environment_revision, record->metadata.revision_uuid, 37);
            if (!derive(record, layout, TIRED_FILE_TARGET_ENVIRONMENT, &record->environment_path,
                        error) ||
                !tired_environment_encode(&record->environment, &environment, error) ||
                !tired_digest_bytes(environment.data, environment.length,
                                    record->environment_sha256, error))
                goto done;
        }
        else
        {
            tired_text_destroy(&record->environment_path);
            record->environment_revision[0] = record->environment_sha256[0] = '\0';
        }
        if (!tired_render_unit(spec, record->metadata.service_uuid,
                               record->has_environment ? &record->environment_path : NULL,
                               &record->credentials, &unit, error) ||
            !tired_digest_bytes(unit.data, unit.length, record->metadata.unit_sha256, error))
            goto done;
    }
    else if (mutation->operation == TIRED_TRANSACTION_START ||
             mutation->operation == TIRED_TRANSACTION_RESTART ||
             (mutation->operation == TIRED_TRANSACTION_ENABLE && mutation->now))
    {
        if (!capture_record(record, &invocation, error))
            goto done;
    }
    ok = tired_mutation_digest(mutation, record->review.approved_sha256, error);
done:
    tired_invocation_destroy(&invocation);
    tired_account_destroy(&account);
    tired_group_destroy(&group);
    tired_text_destroy(&unit);
    tired_text_destroy(&environment);
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_INVALID, "incomplete-proposal",
                        "Proposal is incomplete or exceeds its limits.", 0);
    return ok;
}
static bool select_name(TiredPlan *plan, const TiredBackend *backend, bool explicit_name,
                        TiredError *error)
{
    TiredText base = {0}, candidate = {0};
    const TiredText *original = &plan->spec.fields[TIRED_FIELD_NAME].value.text;
    if (!tired_text_set(&base, original->data, original->length, 255, error))
        return false;
    uint64_t deadline = tired_monotonic_usec() + 5000000;
    bool ok = false;
    for (uint64_t suffix = 1; tired_monotonic_usec() < deadline; ++suffix)
    {
        TiredRuntime runtime = {0};
        TiredCollision collision = {0};
        TiredTextList pending = {0};
        bool queried = tired_name_candidate(&base, suffix, &candidate, error) &&
                       backend->query(backend->context, candidate.data, false, &runtime, error);
        TiredUnitQueryResult observation = {.done = queried,
                                            .file_found = runtime.found,
                                            .object_found = runtime.loaded,
                                            .unit_name = candidate.data};
        bool checked =
            queried && tired_collision_check(&candidate, &observation, backend->load_paths,
                                             &pending, &collision, error);
        bool available = checked && collision.kind == TIRED_COLLISION_NONE;
        tired_runtime_destroy(&runtime);
        tired_collision_destroy(&collision);
        if (!checked)
            break;
        if (available)
        {
            ok = tired_spec_set(&plan->spec, TIRED_FIELD_NAME, candidate.data, candidate.length - 8,
                                TIRED_ORIGIN_CAPTURE, true, error);
            if (ok)
                plan->spec.fields[TIRED_FIELD_NAME].origin =
                    explicit_name ? TIRED_ORIGIN_USER : TIRED_ORIGIN_CAPTURE;
            if (ok && !plan->spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].inherit &&
                (plan->spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].origin == TIRED_ORIGIN_CAPTURE ||
                 plan->spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].origin == TIRED_ORIGIN_DEFAULT))
                ok = tired_spec_set(&plan->spec, TIRED_FIELD_SYSLOG_IDENTIFIER, candidate.data,
                                    candidate.length - 8, TIRED_ORIGIN_CAPTURE, true, error);
            break;
        }
        if (explicit_name)
        {
            tired_error_set(error, TIRED_CONFLICT, "name-collision",
                            "Explicit unit name already exists or is reserved.", 0);
            break;
        }
    }
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_CONFLICT, "name-selection-timeout",
                        "Name discovery exceeded its deadline.", 0);
    tired_text_destroy(&base);
    tired_text_destroy(&candidate);
    return ok;
}
static bool load_existing(const TiredRequest *request, const TiredLayout *layout,
                          const TiredBackend *backend, TiredMutation *mutation, TiredError *error)
{
    TiredText base = {0}, unit = {0};
    TiredServiceInventory inventory = {0};
    TiredServiceFiles files = {0};
    TiredDirectory *directory = NULL;
    TiredRuntime runtime = {0};
    TiredFileFingerprint fingerprint = {0};
    const TiredServiceMetadata *metadata = NULL;
    bool ok = false;
    const TiredText *operand = &request->arguments.items[0];
    if (!tired_name_explicit(operand->data, operand->length, &base, error) ||
        !tired_name_candidate(&base, 1, &unit, error))
        goto done;
    if (!layout->user_scope && getuid() != 0)
    {
        TiredTransactionOperation operation = mutation->operation;
        if (!tired_helper_record_call(unit.data,
                                      request->command == TIRED_COMMAND_EDIT ||
                                          request->command == TIRED_COMMAND_RENAME,
                                      !request->json && isatty(STDIN_FILENO), mutation, error))
            goto done;
        mutation->operation = operation;
        if (!request->restore_drift && strcmp(mutation->expected_unit_sha256.data,
                                              mutation->proposed.metadata.unit_sha256) != 0)
        {
            tired_error_set(error, TIRED_CONFLICT, "unit-drift",
                            "Inspect show/doctor before choosing --restore-managed.", 0);
            goto done;
        }
    }
    else
    {
        if (!tired_service_inventory_load(layout, &inventory, error) ||
            !tired_service_inventory_find(&inventory, unit.data, &metadata, error) ||
            !tired_service_record_load(layout, metadata->service_uuid, &mutation->proposed,
                                       error) ||
            !tired_service_files_inspect(layout, &mutation->proposed, &files, error))
            goto done;
        if (files.unit.state != TIRED_SERVICE_FILE_MATCH &&
            !(request->restore_drift && files.unit.state == TIRED_SERVICE_FILE_DRIFTED &&
              files.unit.owner_matches && files.unit.mode_matches && files.unit.marker_matches))
        {
            tired_error_set(
                error, TIRED_CONFLICT, "unit-drift",
                "Unit is missing, unsafe or changed. Inspect show/doctor before editing.", 0);
            goto done;
        }
        char filename[42];
        (void)snprintf(filename, sizeof(filename), "%s.json", metadata->service_uuid);
        if (!tired_directory_open(layout->paths[TIRED_PATH_RECORDS].data, metadata->owner_uid, true,
                                  &directory, error) ||
            !tired_file_fingerprint(directory, filename, TIRED_SERVICE_RECORD_LIMIT, &fingerprint,
                                    error) ||
            !text(&mutation->expected_record_sha256, fingerprint.sha256, error) ||
            !text(&mutation->expected_unit_sha256, files.unit.actual.sha256, error))
            goto done;
    }
    if (!tired_uuid_create(mutation->proposed.metadata.transaction_uuid, error) ||
        !backend->query(backend->context, unit.data, true, &runtime, error))
        goto done;
    struct timespec updated;
    if (clock_gettime(CLOCK_REALTIME, &updated) != 0)
        goto done;
    mutation->proposed.metadata.updated_usec =
        (uint64_t)updated.tv_sec * 1000000 + (uint64_t)updated.tv_nsec / 1000;
    if (request->command == TIRED_COMMAND_EDIT || request->command == TIRED_COMMAND_RENAME)
    {
        if (!mutation->proposed.environment_loaded &&
            !tired_service_record_hydrate(&mutation->proposed, layout, error))
            goto done;
        if (request->command == TIRED_COMMAND_EDIT && request->seen[TIRED_FIELD_NAME])
        {
            tired_error_set(error, TIRED_INVALID, "explicit-rename-required",
                            "Use rename NAME NEW_NAME to transfer the service identity.", 0);
            goto done;
        }
        mutation->defer = request->apply_mode.data != NULL
                              ? strcmp(request->apply_mode.data, "defer") == 0
                              : !runtime.active;
        if (request->seen[TIRED_FIELD_START] && request->apply_mode.data == NULL)
            mutation->defer = false;
        if (!tired_uuid_create(mutation->proposed.metadata.revision_uuid, error))
            goto done;
        if (!request->seen[TIRED_FIELD_ENABLE] &&
            !tired_spec_set(&mutation->proposed.spec, TIRED_FIELD_ENABLE,
                            runtime.enabled ? "true" : "false", runtime.enabled ? 4 : 5,
                            TIRED_ORIGIN_CAPTURE, true, error))
            goto done;
        if (!request->seen[TIRED_FIELD_START] &&
            !tired_spec_set(&mutation->proposed.spec, TIRED_FIELD_START,
                            runtime.active ? "true" : "false", runtime.active ? 4 : 5,
                            TIRED_ORIGIN_CAPTURE, true, error))
            goto done;
    }
    if (request->command == TIRED_COMMAND_ENABLE || request->command == TIRED_COMMAND_DISABLE)
    {
        bool enabling = request->command == TIRED_COMMAND_ENABLE;
        if (!tired_spec_set(&mutation->proposed.spec, TIRED_FIELD_ENABLE,
                            enabling ? "true" : "false", enabling ? 4 : 5, TIRED_ORIGIN_USER, true,
                            error))
            goto done;
    }
    TiredAccount actor = {0};
    if (!tired_invoking_account(layout->user_scope, &actor, error))
        goto done;
    mutation->actor_uid = actor.uid;
    tired_account_destroy(&actor);
    mutation->now = request->now;
    mutation->keep_history = request->keep_history;
    mutation->restore_drift = request->restore_drift;
    mutation->proposed.review.acknowledged[TIRED_RISK_RESTORE_DRIFT] = false;
    if (!tired_spec_set(&mutation->proposed.spec, TIRED_FIELD_ENABLE_LINGER, "false", 5,
                        TIRED_ORIGIN_CAPTURE, true, error))
        goto done;
    for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (request->seen[i] && i != TIRED_FIELD_SCOPE && i != TIRED_FIELD_WORKING_DIRECTORY)
        {
            tired_mutation_change(mutation, (TiredFieldId)i);
            if (!tired_spec_copy_field(&mutation->proposed.spec, &request->overrides,
                                       (TiredFieldId)i, error))
                goto done;
        }
    if (request->working_directory.data != NULL)
    {
        TiredText cwd = {0};
        char *current = getcwd(NULL, 0);
        TiredText capture = {.data = current, .length = current == NULL ? 0 : strlen(current)};
        bool changed = current != NULL &&
                       tired_path_absolute(&capture, request->working_directory.data,
                                           request->working_directory.length, &cwd, error) &&
                       tired_spec_set(&mutation->proposed.spec, TIRED_FIELD_WORKING_DIRECTORY,
                                      cwd.data, cwd.length, TIRED_ORIGIN_USER, true, error);
        free(current);
        tired_text_destroy(&cwd);
        if (!changed)
            goto done;
    }
    if (request->command == TIRED_COMMAND_RENAME)
    {
        const TiredText *new_name = &request->arguments.items[1];
        if (!tired_name_explicit(new_name->data, new_name->length, &base, error) ||
            !text(&mutation->previous_name, unit.data, error) ||
            !tired_spec_set(&mutation->proposed.spec, TIRED_FIELD_NAME, base.data, base.length,
                            TIRED_ORIGIN_CAPTURE, true, error))
            goto done;
        mutation->proposed.spec.fields[TIRED_FIELD_NAME].origin = TIRED_ORIGIN_USER;
        bool recorded = false;
        for (size_t i = 0; i < mutation->proposed.former_unit_names.count; ++i)
            recorded |= strcmp(mutation->proposed.former_unit_names.items[i].data, unit.data) == 0;
        if (!recorded && !tired_text_list_append(&mutation->proposed.former_unit_names, unit.data,
                                                 unit.length, 256, TIRED_INPUT_LIMIT, error))
            goto done;
        mutation->defer = false;
    }
    ok = true;
done:
    tired_directory_destroy(directory);
    tired_runtime_destroy(&runtime);
    tired_service_inventory_destroy(&inventory);
    tired_text_destroy(&base);
    tired_text_destroy(&unit);
    return ok;
}
static bool response_text(const TiredText *response, TiredText *output, TiredError *error)
{
    struct json_object *document = NULL, *value = NULL, *service = NULL, *runtime = NULL,
                       *installation = NULL;
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok =
        tired_json_parse(response->data, response->length, TIRED_INPUT_LIMIT, &document, error);
    const char *keys[] = {"outcome", "message", "error", "transaction"};
    for (size_t i = 0; ok && i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (json_object_object_get_ex(document, keys[i], &value) &&
            json_object_is_type(value, json_type_string))
        {
            TiredText safe = {0};
            const char *text_value = json_object_get_string(value);
            ok = tired_encode_display(text_value, strlen(text_value), &safe, error) &&
                 tired_buffer_append(&buffer, keys[i], strlen(keys[i]), error) &&
                 tired_buffer_append(&buffer, ": ", 2, error) &&
                 tired_buffer_append(&buffer, safe.data, safe.length, error) &&
                 tired_buffer_append(&buffer, "\n", 1, error);
            tired_text_destroy(&safe);
        }
    if (ok && json_object_object_get_ex(document, "service", &service) &&
        json_object_object_get_ex(service, "unit", &value))
    {
        const char *name = json_object_get_string(value);
        ok = tired_buffer_append(&buffer, "Service: ", 9, error) &&
             tired_buffer_append(&buffer, name, strlen(name), error) &&
             tired_buffer_append(&buffer, "\n", 1, error);
    }
    if (ok && json_object_object_get_ex(document, "installation", &installation))
    {
        if (json_object_object_get_ex(installation, "state", &value))
        {
            const char *state = json_object_get_string(value);
            ok = tired_buffer_append(&buffer, "Installation: ", 14, error) &&
                 tired_buffer_append(&buffer, state, strlen(state), error) &&
                 tired_buffer_append(&buffer, "\n", 1, error);
        }
        if (ok && json_object_object_get_ex(installation, "enabled", &value))
        {
            const char *state = value == NULL                    ? "unknown"
                                : json_object_get_boolean(value) ? "yes"
                                                                 : "no";
            ok = tired_buffer_append(&buffer, "Enabled: ", 9, error) &&
                 tired_buffer_append(&buffer, state, strlen(state), error) &&
                 tired_buffer_append(&buffer, "\n", 1, error);
        }
    }
    if (ok && json_object_object_get_ex(document, "runtime", &runtime))
    {
        if (json_object_object_get_ex(runtime, "active_state", &value))
        {
            const char *state = value == NULL ? "unknown" : json_object_get_string(value);
            ok = tired_buffer_append(&buffer, "Runtime: ", 9, error) &&
                 tired_buffer_append(&buffer, state, strlen(state), error) &&
                 tired_buffer_append(&buffer, "\n", 1, error);
        }
        if (ok && json_object_object_get_ex(runtime, "earlier_start_context", &value) &&
            json_object_get_boolean(value))
            ok = tired_buffer_append(
                &buffer,
                "Configuration changed; the running process retains its earlier start context.\n",
                sizeof("Configuration changed; the running process retains its earlier start "
                       "context.\n") -
                    1,
                error);
    }
    if (ok)
        ok = tired_buffer_append(
                 &buffer,
                 "Inspect: tired status NAME; tired logs NAME --follow\nManage: tired edit NAME; "
                 "tired stop NAME; tired remove NAME\nApplication health is not verified. systemd "
                 "supervises installed services independently of tired.\n",
                 sizeof(
                     "Inspect: tired status NAME; tired logs NAME --follow\nManage: tired edit "
                     "NAME; tired stop NAME; tired remove NAME\nApplication health is not "
                     "verified. systemd supervises installed services independently of tired.\n") -
                     1,
                 error) &&
             tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    json_object_put(document);
    return ok;
}
bool tired_frontend_command(const TiredRequest *request, const char *profiles, TiredText *output,
                            TiredStatus *status, TiredError *error)
{
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredPlan plan = {0};
    TiredSettings settings = {0};
    TiredProfileCatalog catalog = {0};
    TiredMutation mutation = {0};
    TiredText response = {0};
    TiredRiskReport risks = {0};
    bool ok = false;
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    if (!tired_layout_discover(user, &layout, error) ||
        !tired_config_discover(request, &settings, error) ||
        !tired_backend_open(&layout, &native, &backend, error))
        goto done;
    tired_ui_presentation(settings.ascii);
    if (request->command == TIRED_COMMAND_RECOVER && request->resolution.data != NULL)
    {
        if (!request->yes || request->transaction.data == NULL ||
            !tired_uuid_valid(request->transaction.data, request->transaction.length))
        {
            tired_error_set(error, TIRED_INVALID, "recovery-approval",
                            "Recovery resolution requires --transaction UUID --resolution "
                            "finish|rollback --yes.",
                            0);
            goto done;
        }
        if (!tired_helper_recover_call(
                user, request->transaction.data, strcmp(request->resolution.data, "finish") == 0,
                !request->json && isatty(STDIN_FILENO), &response, status, error))
            goto done;
        ok = request->json
                 ? tired_text_set(output, response.data, response.length, TIRED_INPUT_LIMIT, error)
                 : response_text(&response, output, error);
        goto done;
    }
    if (request->command == TIRED_COMMAND_CREATE || request->command == TIRED_COMMAND_PLAN)
    {
        if (!tired_plan_prepare_settings(request, &settings, &plan, error))
            goto done;
        if (request->profile.data == NULL || strcmp(request->profile.data, "none") != 0)
            if (!tired_profiles_discover_settings(profiles, user, &settings, &catalog, error) ||
                !tired_plan_apply_profiles(&plan, &catalog, request->profile.data,
                                           &backend.features, error))
                goto done;
        if (!select_name(&plan, &backend, request->seen[TIRED_FIELD_NAME], error) ||
            !tired_mutation_from_plan(&plan, &layout, &settings, &request->allowed_risks, &mutation,
                                      error))
            goto done;
    }
    else
    {
        switch (request->command)
        {
        case TIRED_COMMAND_START:
            mutation.operation = TIRED_TRANSACTION_START;
            break;
        case TIRED_COMMAND_STOP:
            mutation.operation = TIRED_TRANSACTION_STOP;
            break;
        case TIRED_COMMAND_RESTART:
            mutation.operation = TIRED_TRANSACTION_RESTART;
            break;
        case TIRED_COMMAND_ENABLE:
            mutation.operation = TIRED_TRANSACTION_ENABLE;
            break;
        case TIRED_COMMAND_DISABLE:
            mutation.operation = TIRED_TRANSACTION_DISABLE;
            break;
        case TIRED_COMMAND_EDIT:
            mutation.operation = TIRED_TRANSACTION_EDIT;
            break;
        case TIRED_COMMAND_RENAME:
            mutation.operation = TIRED_TRANSACTION_RENAME;
            break;
        case TIRED_COMMAND_REMOVE:
            mutation.operation = TIRED_TRANSACTION_REMOVE;
            break;
        default:
            tired_error_set(error, TIRED_INVALID, "mutation-command",
                            "Expected a service mutation command.", 0);
            goto done;
        }
        mutation.observation_usec = settings.observation_usec;
        mutation.history_revisions = settings.history_revisions;
        if (!load_existing(request, &layout, &backend, &mutation, error))
            goto done;
        if (request->command == TIRED_COMMAND_EDIT &&
            !tired_edit_inputs(request, &mutation, &backend, &settings, profiles, error))
            goto done;
    }
    if (request->command == TIRED_COMMAND_CREATE)
        mutation.proposed.metadata.interactive =
            !request->yes && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    for (size_t i = 0; i < request->allowed_risks.count; ++i)
    {
        TiredRiskId id;
        if (tired_risk_find(request->allowed_risks.items[i].data,
                            request->allowed_risks.items[i].length, &id))
            mutation.proposed.review.acknowledged[id] = true;
    }
    if (!tired_mutation_refresh(&mutation, &layout, error) ||
        !tired_mutation_validate(&mutation, &layout, &backend, &risks, error))
        goto done;
    if (request->command == TIRED_COMMAND_PLAN)
    {
        tired_text_destroy(&plan.managed_environment);
        if (mutation.proposed.has_environment &&
            !text(&plan.managed_environment, mutation.proposed.environment_path.data, error))
            goto done;
        plan.live_validated = true;
        plan.live_risks = risks;
        plan.linger_known = backend.linger_known;
        plan.linger_enabled = backend.linger_enabled;
        ok = tired_plan_output(&plan, request->json, request->unit, request->include_sensitive,
                               output, error);
        if (ok && request->output.data != NULL)
        {
            ok = tired_write_private_new(request->output.data, output->data, output->length, error);
            if (ok)
                ok = text(
                    output,
                    request->json
                        ? "{\"schema_version\":1,\"ok\":true,\"exit_code\":0,\"exported\":true}\n"
                        : "Wrote a private validated plan.\n",
                    error);
        }
        goto done;
    }
    bool lifecycle =
        request->command == TIRED_COMMAND_START || request->command == TIRED_COMMAND_STOP ||
        request->command == TIRED_COMMAND_RESTART || request->command == TIRED_COMMAND_ENABLE ||
        request->command == TIRED_COMMAND_DISABLE;
    if (!request->yes && !lifecycle)
    {
        if (request->json || (!isatty(STDIN_FILENO) && !isatty(STDOUT_FILENO)))
        {
            tired_error_set(
                error, TIRED_INVALID, "approval-required",
                "Noninteractive mutation requires --yes and explicit --allow-risk codes.", 0);
            goto done;
        }
        for (;;)
        {
            bool monochrome = request->color.data != NULL
                                  ? strcmp(request->color.data, "never") == 0
                                  : settings.color == 2;
            if (tired_ui_review(&mutation, &layout, &backend, &settings,
                                request->no_tui || !settings.tui, monochrome, error))
                break;
            if (error->code != NULL && strcmp(error->code, "review-refresh-profile") == 0)
            {
                TiredRequest refresh = {.refresh_profile = true};
                if (!tired_edit_inputs(&refresh, &mutation, &backend, &settings, profiles, error) ||
                    !tired_mutation_refresh(&mutation, &layout, error))
                    goto done;
                continue;
            }
            if (error->code == NULL || strcmp(error->code, "review-scope-changed") != 0)
                goto done;
            bool next_user =
                tired_spec_choice_is(&mutation.proposed.spec, TIRED_FIELD_SCOPE, "user");
            tired_backend_destroy(native);
            native = NULL;
            tired_layout_destroy(&layout);
            if (!tired_layout_discover(next_user, &layout, error) ||
                !tired_backend_open(&layout, &native, &backend, error))
                goto done;
            mutation.proposed.metadata.user_scope = next_user;
            mutation.proposed.metadata.owner_uid = next_user ? getuid() : 0;
            if (next_user)
            {
                char selector[32], group_selector[32];
                (void)snprintf(selector, sizeof(selector), "%lu", (unsigned long)getuid());
                (void)snprintf(group_selector, sizeof(group_selector), "%lu",
                               (unsigned long)getgid());
                if (!tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_RUN_AS, selector,
                                    strlen(selector), TIRED_ORIGIN_CAPTURE, true, error) ||
                    !tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_GROUP, group_selector,
                                    strlen(group_selector), TIRED_ORIGIN_CAPTURE, true, error))
                    goto done;
            }
            else if (mutation.proposed.spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean &&
                     !tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_ENABLE_LINGER, "false", 5,
                                     TIRED_ORIGIN_CAPTURE, true, error))
                goto done;
            TiredRequest refresh = {.refresh_profile = true};
            if (mutation.proposed.has_profile && !next_user &&
                mutation.proposed.profile.source_origin == TIRED_PROFILE_USER)
                refresh.profile = (TiredText){.data = "auto", .length = 4};
            memset(mutation.proposed.review.acknowledged, 0,
                   sizeof(mutation.proposed.review.acknowledged));
            if (!tired_edit_inputs(&refresh, &mutation, &backend, &settings, profiles, error) ||
                !tired_mutation_refresh(&mutation, &layout, error) ||
                !tired_mutation_validate(&mutation, &layout, &backend, &risks, error))
                goto done;
        }
    }
    mutation.proposed.linger_requested |=
        mutation.proposed.spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean;
    if (!(request->yes || lifecycle
              ? tired_mutation_refresh(&mutation, &layout, error)
              : tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, error)))
        goto done;
    if (!tired_mutation_validate(&mutation, &layout, &backend, &risks, error))
        goto done;
    for (size_t i = 0; i < TIRED_RISK_COUNT; ++i)
        if (risks.pending[i] || (risks.present[i] && !mutation.proposed.review.acknowledged[i]))
        {
            tired_error_set(
                error, TIRED_INVALID, "risk-acknowledgment",
                "Required risks need explicit acknowledgment; --yes does not acknowledge risks.",
                0);
            goto done;
        }
    if (!tired_helper_call(&mutation, !request->json && isatty(STDIN_FILENO), &response, status,
                           error))
        goto done;
    if (request->verbose)
        fprintf(stderr, "tired: transaction %s; recorded result code %d\n",
                mutation.proposed.metadata.transaction_uuid, *status);
    struct json_object *result_document = NULL;
    if (!tired_json_parse(response.data, response.length, TIRED_INPUT_LIMIT, &result_document,
                          error))
        goto done;
    struct json_object *command = json_object_new_string(tired_command_name(request->command));
    if (command == NULL || json_object_object_add(result_document, "command", command) != 0)
    {
        json_object_put(command);
        json_object_put(result_document);
        goto done;
    }
    const char *encoded = json_object_to_json_string_ext(result_document, JSON_C_TO_STRING_PLAIN);
    bool encoded_ok = encoded != NULL && text(&response, encoded, error);
    json_object_put(result_document);
    if (!encoded_ok)
        goto done;
    ok = request->json
             ? tired_text_set(output, response.data, response.length, TIRED_INPUT_LIMIT, error)
             : response_text(&response, output, error);
done:
    tired_text_destroy(&response);
    tired_mutation_destroy(&mutation);
    tired_plan_destroy(&plan);
    tired_catalog_destroy(&catalog);
    tired_settings_destroy(&settings);
    tired_backend_destroy(native);
    tired_layout_destroy(&layout);
    return ok;
}
bool tired_frontend_dashboard(const TiredRequest *request, const char *profiles, TiredText *output,
                              TiredStatus *status, TiredError *error)
{
    TiredSettings settings = {0};
    if (!tired_config_discover(request, &settings, error))
        return false;
    bool use_tui = settings.tui && !request->json && tired_ui_usable();
    tired_settings_destroy(&settings);
    if (use_tui)
        return tired_ui_dashboard(request, profiles, status, error);
    TiredRequest list = *request;
    list.command = TIRED_COMMAND_LIST;
    bool ok = tired_list_command(&list, output, status, error);
    if (ok && !request->json)
    {
        TiredBuffer buffer;
        tired_buffer_init(&buffer, TIRED_SERVICE_RECORD_LIMIT);
        ok = tired_buffer_append(&buffer, output->data, output->length, error) &&
             tired_buffer_append(
                 &buffer, "\nCreate a service: tired COMMAND\nHelp: tired --help\n",
                 sizeof("\nCreate a service: tired COMMAND\nHelp: tired --help\n") - 1, error) &&
             tired_buffer_take(&buffer, output, error);
        tired_buffer_destroy(&buffer);
    }
    return ok;
}
