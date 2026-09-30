#define _GNU_SOURCE
#include "tired/backend.h"
#include "tired/encode.h"
#include "tired/helper.h"
#include "tired/identity.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/linger_enable.h"
#include "tired/linger_query.h"
#include "tired/load_paths.h"
#include "tired/manager.h"
#include "tired/manager_enablement.h"
#include "tired/manager_reload.h"
#include "tired/name.h"
#include "tired/verify.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

struct TiredNativeBackend
{
    sd_bus *bus;
    TiredManagerIdentity *identity;
    TiredTextList paths, pinned;
    bool user, interactive, execution_possible, progress;
    uint64_t began;
    TiredEffectSink effect_sink;
    void *effect_context;
};
uint64_t tired_monotonic_usec(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
}
static bool wait_bus(sd_bus *bus, TiredError *error)
{
    int rc = sd_bus_wait(bus, 50000);
    return rc >= 0 || rc == -EINTR ||
           tired_error_set(error, TIRED_UNSUPPORTED, "manager-disconnected",
                           "Cannot communicate with the selected systemd manager.", -rc);
}
void tired_runtime_destroy(TiredRuntime *runtime)
{
    if (runtime == NULL)
        return;
    tired_text_destroy(&runtime->active_state);
    tired_text_destroy(&runtime->sub_state);
    tired_text_destroy(&runtime->result);
    tired_text_destroy(&runtime->file_state);
    tired_text_destroy(&runtime->fragment);
    tired_text_list_destroy(&runtime->drop_ins);
    *runtime = (TiredRuntime){0};
}
static bool copy_property(const TiredUnitObservation *observation, TiredObservationId id,
                          TiredText *text, TiredError *error)
{
    if (observation == NULL || !observation->fields[id].known)
        return true;
    const TiredText *value = &observation->fields[id].value.text;
    return tired_text_set(text, value->data, value->length, TIRED_INPUT_LIMIT, error);
}
static bool equals(const TiredText *text, const char *value)
{
    return text->data != NULL && strcmp(text->data, value) == 0;
}
static bool query(void *context, const char *name, bool load, TiredRuntime *output,
                  TiredError *error)
{
    TiredNativeBackend *native = context;
    TiredText base = {0};
    TiredUnitQuery *operation = NULL;
    TiredRuntime state = {0};
    bool ok = false;
    if (!tired_name_explicit(name, strlen(name), &base, error) ||
        !tired_unit_query_start_lookup(
            native->identity, &base, load ? TIRED_UNIT_LOAD_CONFIGURATION : TIRED_UNIT_LOADED_ONLY,
            5000, &operation, error))
        goto done;
    while (!tired_unit_query_step(operation))
        if (!wait_bus(native->bus, error))
            goto done;
    TiredUnitQueryResult result = tired_unit_query_result(operation);
    if (result.error.status != TIRED_OK)
    {
        *error = result.error;
        goto done;
    }
    state.found = result.file_found;
    state.loaded = result.object_found;
    if (result.file_state != NULL && !tired_text_set(&state.file_state, result.file_state,
                                                     strlen(result.file_state), 255, error))
        goto done;
    if (!copy_property(result.observation, TIRED_OBS_ACTIVE_STATE, &state.active_state, error) ||
        !copy_property(result.observation, TIRED_OBS_SUB_STATE, &state.sub_state, error) ||
        !copy_property(result.observation, TIRED_OBS_RESULT, &state.result, error) ||
        !copy_property(result.observation, TIRED_OBS_FRAGMENT_PATH, &state.fragment, error))
        goto done;
    state.active = equals(&state.active_state, "active") ||
                   equals(&state.active_state, "activating") ||
                   equals(&state.active_state, "deactivating");
    state.running = equals(&state.active_state, "active") && equals(&state.sub_state, "running");
    state.failed =
        equals(&state.active_state, "failed") || equals(&state.sub_state, "auto-restart");
    const TiredObservedValue *exit_time =
        result.observation == NULL ? NULL : &result.observation->fields[TIRED_OBS_EXEC_EXIT];
    state.completed =
        exit_time != NULL && exit_time->known && exit_time->value.unsigned_value != 0 &&
        equals(&state.result, "success") &&
        (equals(&state.sub_state, "exited") || equals(&state.active_state, "inactive"));
    state.enabled = equals(&state.file_state, "enabled");
    state.job_known = !result.object_found;
    if (result.object_found && result.object_path != NULL)
    {
        TiredManagerIdentityResult owner = tired_manager_identity_result(native->identity);
        sd_bus_message *property = NULL;
        sd_bus_error remote = SD_BUS_ERROR_NULL;
        const char *path = NULL;
        if (owner.ready &&
            sd_bus_get_property(native->bus, owner.unique_name, result.object_path,
                                "org.freedesktop.systemd1.Unit", "Job", &remote, &property,
                                "(uo)") >= 0 &&
            sd_bus_message_read(property, "(uo)", &state.job_id, &path) > 0 &&
            tired_manager_identity_result(native->identity).ready)
            state.job_known = true;
        sd_bus_error_free(&remote);
        sd_bus_message_unref(property);
    }
    if (result.object_found && result.observation != NULL)
    {
        const TiredObservedValue *drops = &result.observation->fields[TIRED_OBS_DROP_IN_PATHS];
        if (!drops->known)
        {
            tired_error_set(error, TIRED_UNSUPPORTED, "drop-ins-unknown",
                            "The manager did not report applicable drop-ins.", 0);
            goto done;
        }
        for (size_t i = 0; i < drops->value.list.count; ++i)
            if (!tired_text_list_append(&state.drop_ins, drops->value.list.items[i].data,
                                        drops->value.list.items[i].length, 256, TIRED_INPUT_LIMIT,
                                        error))
                goto done;
        const TiredObservedValue *count = &result.observation->fields[TIRED_OBS_RESTARTS];
        state.restarts_known = count->known;
        state.restarts = count->known ? count->value.unsigned_value : 0;
        const TiredObservedValue *pid = &result.observation->fields[TIRED_OBS_MAIN_PID];
        state.pid = pid->known ? pid->value.unsigned_value : 0;
        const TiredObservedValue *exit_code = &result.observation->fields[TIRED_OBS_EXIT_CODE];
        const TiredObservedValue *exit_status = &result.observation->fields[TIRED_OBS_EXIT_STATUS];
        state.exit_known = exit_code->known && exit_status->known;
        if (state.exit_known)
        {
            state.exit_code = exit_code->value.signed_value;
            state.exit_status = exit_status->value.signed_value;
        }
    }
    tired_runtime_destroy(output);
    *output = state;
    state = (TiredRuntime){0};
    ok = true;
done:
    tired_runtime_destroy(&state);
    tired_text_destroy(&base);
    tired_unit_query_destroy(operation);
    return ok;
}
static bool reload(void *context, bool *uncertain, TiredError *error)
{
    TiredNativeBackend *native = context;
    TiredManagerReload *operation = NULL;
    *uncertain = false;
    if (!tired_manager_reload_start(native->identity, 30000, &operation, error))
        return false;
    bool ok = true;
    while (!tired_manager_reload_step(operation))
        if (!wait_bus(native->bus, error))
        {
            ok = false;
            break;
        }
    TiredManagerReloadResult result = tired_manager_reload_result(operation);
    if (ok && result.error.status != TIRED_OK)
    {
        *error = result.error;
        ok = false;
    }
    *uncertain = result.submitted && !result.acknowledged;
    tired_manager_reload_destroy(operation);
    return ok && result.acknowledged;
}
static bool reset_failed(void *context, const char *name, bool *uncertain, TiredError *error)
{
    TiredNativeBackend *native = context;
    TiredManagerIdentityResult owner = tired_manager_identity_result(native->identity);
    sd_bus_error remote = SD_BUS_ERROR_NULL;
    *uncertain = false;
    int rc = owner.ready
                 ? sd_bus_call_method(native->bus, owner.unique_name, "/org/freedesktop/systemd1",
                                      "org.freedesktop.systemd1.Manager", "ResetFailedUnit",
                                      &remote, NULL, "s", name)
                 : -ENOTCONN;
    bool denied = sd_bus_error_has_name(&remote, "org.freedesktop.DBus.Error.AccessDenied");
    *uncertain = rc < 0 && !denied;
    sd_bus_error_free(&remote);
    return rc >= 0 ||
           tired_error_set(error, denied ? TIRED_AUTHORIZATION : TIRED_RECOVERY_REQUIRED,
                           "reset-failed-unit", "Cannot reset this unit's failed state.", -rc);
}
static void progress(void *context, const char *phase)
{
    TiredNativeBackend *native = context;
    if (native->progress)
        fprintf(stderr, "tired: phase %s; elapsed %.1f s\n", phase,
                (double)(tired_monotonic_usec() - native->began) / 1000000.0);
}
static bool enable(void *context, const char *name, bool value, bool *uncertain, TiredError *error)
{
    TiredNativeBackend *native = context;
    TiredManagerEnablement *operation = NULL;
    *uncertain = false;
    if (!tired_manager_enablement_start(native->identity, name, value, 30000, &operation, error))
        return false;
    bool ok = true;
    while (!tired_manager_enablement_step(operation))
        if (!wait_bus(native->bus, error))
        {
            ok = false;
            break;
        }
    TiredManagerEnablementResult result = tired_manager_enablement_result(operation);
    if (ok && result.error.status != TIRED_OK)
    {
        *error = result.error;
        ok = false;
    }
    *uncertain = result.submitted && !result.acknowledged;
    if (result.acknowledged && native->effect_sink != NULL)
    {
        struct json_object *document = json_object_new_object(), *changes = json_object_new_array();
        bool built = document != NULL && changes != NULL;
        for (size_t i = 0; built && result.changes != NULL && i < result.changes->count; ++i)
        {
            const TiredUnitFileChange *change = &result.changes->items[i];
            struct json_object *row = json_object_new_object();
            built = row != NULL;
            if (built)
            {
                json_object_object_add(row, "type", json_object_new_string(change->type.data));
                json_object_object_add(row, "path", json_object_new_string(change->path.data));
                json_object_object_add(row, "source", json_object_new_string(change->source.data));
                built = json_object_array_add(changes, row) == 0;
                if (!built)
                    json_object_put(row);
            }
        }
        if (built)
        {
            json_object_object_add(document, "unit", json_object_new_string(name));
            json_object_object_add(document, "enable", json_object_new_boolean(value));
            built = json_object_object_add(document, "changes", changes) == 0;
            if (built)
                changes = NULL;
        }
        const char *bytes =
            built ? json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN) : NULL;
        TiredText evidence = {.data = (char *)bytes, .length = bytes == NULL ? 0 : strlen(bytes)};
        if (bytes == NULL ||
            !native->effect_sink(native->effect_context, "enablement_changes", &evidence, error))
        {
            ok = false;
            *uncertain = true;
        }
        json_object_put(changes);
        json_object_put(document);
    }
    tired_manager_enablement_destroy(operation);
    return ok && result.acknowledged;
}
static bool job_event(TiredNativeBackend *native, const char *name, TiredManagerJobResult result,
                      TiredError *error)
{
    if (native->effect_sink == NULL)
        return true;
    struct json_object *document = json_object_new_object();
    if (document == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot record manager job evidence.", 0);
    json_object_object_add(document, "unit", json_object_new_string(name));
    json_object_object_add(document, "job_id", json_object_new_uint64(result.job_id));
    json_object_object_add(document, "submitted", json_object_new_boolean(result.submitted));
    json_object_object_add(document, "accepted", json_object_new_boolean(result.accepted));
    json_object_object_add(document, "finished", json_object_new_boolean(result.finished));
    json_object_object_add(document, "rejected", json_object_new_boolean(result.rejected));
    json_object_object_add(document, "result", json_object_new_string(result.completion.result));
    const char *bytes = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    TiredText event = {.data = (char *)bytes, .length = bytes == NULL ? 0 : strlen(bytes)};
    bool ok = bytes != NULL &&
              native->effect_sink(native->effect_context,
                                  result.finished ? "job_finished" : "job_accepted", &event, error);
    json_object_put(document);
    return ok;
}
static bool job(void *context, const char *name, TiredJobAction action, bool *uncertain,
                TiredError *error)
{
    TiredNativeBackend *native = context;
    *uncertain = false;
    native->execution_possible = false;
    bool pinned = false;
    for (size_t i = 0; i < native->pinned.count; ++i)
        pinned |= strcmp(native->pinned.items[i].data, name) == 0;
    if (!pinned)
    {
        TiredRuntime loaded = {0};
        bool read = query(native, name, true, &loaded, error);
        tired_runtime_destroy(&loaded);
        if (!read)
            return false;
        TiredManagerIdentityResult owner = tired_manager_identity_result(native->identity);
        sd_bus_error remote = SD_BUS_ERROR_NULL;
        int rc = owner.ready ? sd_bus_call_method(native->bus, owner.unique_name,
                                                  "/org/freedesktop/systemd1",
                                                  "org.freedesktop.systemd1.Manager", "RefUnit",
                                                  &remote, NULL, "s", name)
                             : -ENOTCONN;
        sd_bus_error_free(&remote);
        if (rc < 0 || !tired_text_list_append(&native->pinned, name, strlen(name), 4, 1024, error))
            return rc >= 0 ? false
                           : tired_error_set(error, TIRED_UNSUPPORTED, "unit-reference",
                                             "Cannot retain the unit for reliable job and "
                                             "initial-exit observation.",
                                             -rc);
    }
    TiredManagerJob *operation = NULL;
    if (!tired_manager_job_start(native->identity, name, action, 120000, &operation, error))
        return false;
    bool ok = true, recorded = false;
    while (!tired_manager_job_step(operation))
    {
        TiredManagerJobResult current = tired_manager_job_result(operation);
        if (current.accepted && !recorded)
        {
            if (!job_event(native, name, current, error))
            {
                ok = false;
                break;
            }
            recorded = true;
        }
        if (!wait_bus(native->bus, error))
        {
            ok = false;
            break;
        }
    }
    TiredManagerJobResult result = tired_manager_job_result(operation);
    native->execution_possible = result.submitted && !result.rejected;
    *uncertain = result.submitted && !result.finished && !result.rejected;
    if (ok && !job_event(native, name, result, error))
    {
        ok = false;
        *uncertain |= result.submitted && !result.rejected;
    }
    if (ok && result.error.status != TIRED_OK)
    {
        *error = result.error;
        ok = false;
    }
    tired_manager_job_destroy(operation);
    return ok && result.finished;
}
static void effects(void *context, TiredEffectSink sink, void *sink_context)
{
    TiredNativeBackend *native = context;
    native->effect_sink = sink;
    native->effect_context = sink_context;
}
static bool execution_possible(void *context)
{
    return ((TiredNativeBackend *)context)->execution_possible;
}
static bool verify(void *context, const TiredServiceSpec *spec, const TiredText *unit,
                   TiredError *error)
{
    TiredNativeBackend *native = context;
    TiredVerification *operation = NULL;
    TiredAccount account = {0};
    TiredLayout layout = {0};
    TiredText data = {0};
    TiredVerifyUserPaths paths = {0};
    bool ok = false;
    if (native->user)
    {
        if (!tired_account_by_uid(getuid(), &account, error) ||
            !tired_layout_discover(true, &layout, error))
            goto done;
        const char *xdg_data = getenv("XDG_DATA_HOME");
        if (xdg_data == NULL || xdg_data[0] == '\0')
        {
            if (!tired_text_set(&data, account.home.data, account.home.length, 4096, error))
                goto done;
            TiredBuffer buffer;
            tired_buffer_init(&buffer, 4096);
            bool built = tired_buffer_append(&buffer, data.data, data.length, error) &&
                         tired_buffer_append(&buffer, "/.local/share", 13, error) &&
                         tired_buffer_take(&buffer, &data, error);
            tired_buffer_destroy(&buffer);
            if (!built)
                goto done;
            xdg_data = data.data;
        }
        /* config_home is the XDG root, not its systemd/user child. */
        TiredText config = {0};
        const TiredText *units = &layout.paths[TIRED_PATH_UNITS];
        if (units->length < 13 ||
            !tired_text_set(&config, units->data, units->length - 13, 4096, error))
            goto done;
        paths = (TiredVerifyUserPaths){.home = account.home.data,
                                       .config_home = config.data,
                                       .data_home = xdg_data,
                                       .runtime_directory = getenv("XDG_RUNTIME_DIR")};
        ok = tired_verify_start(&spec->fields[TIRED_FIELD_NAME].value.text, unit->data,
                                unit->length, &paths, 30000, &operation, error);
        tired_text_destroy(&config);
    }
    else
        ok = tired_verify_start(&spec->fields[TIRED_FIELD_NAME].value.text, unit->data,
                                unit->length, NULL, 30000, &operation, error);
    if (!ok)
        goto done;
    while (!tired_verify_step(operation))
    {
        struct timespec pause = {.tv_nsec = 10000000};
        (void)nanosleep(&pause, NULL);
    }
    TiredVerifyResult result = tired_verify_result(operation);
    ok = result.cleanup_complete &&
         (result.state == TIRED_VERIFY_CLEAN || result.state == TIRED_VERIFY_DIAGNOSTICS);
    if (!ok)
        tired_error_set(error, result.cleanup_complete ? TIRED_INVALID : TIRED_RECOVERY_REQUIRED,
                        "unit-verification", "Staged systemd unit verification failed.",
                        result.process.system_errno);
done:
    tired_verify_destroy(operation);
    tired_account_destroy(&account);
    tired_layout_destroy(&layout);
    tired_text_destroy(&data);
    return ok;
}
static bool linger(void *context, uid_t uid, bool *uncertain, TiredError *error)
{
    TiredNativeBackend *native = context;
    sd_bus *bus = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredLingerEnable *operation = NULL;
    TiredLingerQuery *query = NULL;
    bool ok = false;
    *uncertain = false;
    if (!tired_manager_bus_open(false, &bus, error) ||
        !tired_login_identity_start(bus, 5000, &identity, error))
        goto done;
    while (!tired_manager_identity_step(identity))
        if (!wait_bus(bus, error))
            goto done;
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready)
    {
        *error = owner.error;
        goto done;
    }
    if (tired_linger_query_start(identity, uid, 5000, &query, error))
    {
        while (!tired_linger_query_step(query))
            if (!wait_bus(bus, error))
                goto done;
        TiredLingerResult status = tired_linger_query_result(query);
        if (status.known && status.enabled)
        {
            ok = true;
            tired_error_clear(error);
            goto done;
        }
    }
    tired_error_clear(error);
    if (native->user && getuid() != 0)
    {
        /* The user transaction has already recorded this explicit account-change
         * intent. The administrator endpoint receives only the current UID; it
         * never reads user-selected paths or writes user service files. */
        ok = tired_helper_linger_call(uid, native->interactive, uncertain, error);
        goto done;
    }
    if (!tired_linger_enable_start(identity, uid, false, 30000, &operation, error))
        goto done;
    while (!tired_linger_enable_step(operation))
        if (!wait_bus(bus, error))
            goto done;
    TiredLingerEnableResult result = tired_linger_enable_result(operation);
    *uncertain = result.submitted && !result.observed;
    if (result.error.status != TIRED_OK)
        *error = result.error;
    ok = result.error.status == TIRED_OK && result.observed && result.enabled;
done:
    if (operation != NULL && !ok)
        *uncertain |= tired_linger_enable_result(operation).submitted;
    tired_linger_enable_destroy(operation);
    tired_linger_query_destroy(query);
    tired_manager_identity_destroy(identity);
    sd_bus_close_unref(bus);
    return ok;
}
static void linger_status(TiredBackend *backend)
{
    sd_bus *bus = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredLingerQuery *query = NULL;
    TiredError error = {0};
    if (!tired_manager_bus_open(false, &bus, &error) ||
        !tired_login_identity_start(bus, 3000, &identity, &error))
        goto done;
    while (!tired_manager_identity_step(identity))
        if (!wait_bus(bus, &error))
            goto done;
    if (!tired_manager_identity_result(identity).ready ||
        !tired_linger_query_start(identity, getuid(), 3000, &query, &error))
        goto done;
    while (!tired_linger_query_step(query))
        if (!wait_bus(bus, &error))
            goto done;
    TiredLingerResult status = tired_linger_query_result(query);
    backend->linger_known = status.known;
    backend->linger_enabled = status.known && status.enabled;
done:
    tired_linger_query_destroy(query);
    tired_manager_identity_destroy(identity);
    sd_bus_close_unref(bus);
}
static bool word(const TiredText *text, const char *token)
{
    size_t length = strlen(token);
    for (size_t i = 0; i < text->length;)
    {
        while (i < text->length && (text->data[i] == ' ' || text->data[i] == '\n'))
            ++i;
        size_t start = i;
        while (i < text->length && text->data[i] != ' ' && text->data[i] != '\n')
            ++i;
        if (i - start == length && memcmp(text->data + start, token, length) == 0)
            return true;
    }
    return false;
}
static void resource_features(TiredNativeBackend *native, TiredBackend *backend)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(native->identity);
    char *group = NULL;
    sd_bus_error remote = SD_BUS_ERROR_NULL;
    TiredText path = {0}, controllers = {0};
    TiredError ignored = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 4096);
    if (sd_bus_get_property_string(native->bus, owner.unique_name, "/org/freedesktop/systemd1",
                                   "org.freedesktop.systemd1.Manager", "ControlGroup", &remote,
                                   &group) >= 0 &&
        group != NULL && group[0] == '/' && strstr(group, "..") == NULL &&
        tired_buffer_append(&buffer, "/sys/fs/cgroup", 14, &ignored) &&
        (strcmp(group, "/") == 0 || tired_buffer_append(&buffer, group, strlen(group), &ignored)) &&
        tired_buffer_append(&buffer, "/cgroup.controllers", 19, &ignored) &&
        tired_buffer_take(&buffer, &path, &ignored) &&
        tired_read_file(path.data, 4096, &controllers, &ignored))
    {
        const char *names[] = {"memory", "cpu", "pids"};
        for (size_t i = 0; i < 3; ++i)
            backend->features.features[i + 1] =
                word(&controllers, names[i]) ? TIRED_FACT_TRUE : TIRED_FACT_FALSE;
    }
    backend->features.features[4] = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, 0, 0, 0) >= 0
                                        ? TIRED_FACT_TRUE
                                    : errno == EINVAL ? TIRED_FACT_FALSE
                                                      : TIRED_FACT_UNKNOWN;
    free(group);
    sd_bus_error_free(&remote);
    tired_buffer_destroy(&buffer);
    tired_text_destroy(&path);
    tired_text_destroy(&controllers);
}
bool tired_backend_open(const TiredLayout *layout, TiredNativeBackend **output,
                        TiredBackend *backend, TiredError *error)
{
    TiredNativeBackend *native = calloc(1, sizeof(*native));
    TiredManagerProbe *probe = NULL;
    TiredLoadPaths *paths = NULL;
    bool ok = false;
    if (native == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot open manager.", 0);
    native->user = layout->user_scope;
    if (!tired_manager_bus_open(native->user, &native->bus, error) ||
        !tired_manager_identity_start(native->bus, native->user, 5000, &native->identity, error))
        goto done;
    while (!tired_manager_identity_step(native->identity))
        if (!wait_bus(native->bus, error))
            goto done;
    TiredManagerIdentityResult identity = tired_manager_identity_result(native->identity);
    if (!identity.ready)
    {
        *error = identity.error;
        goto done;
    }
    if (!tired_manager_probe_start_unique(native->bus, identity.unique_name, 5000, &probe, error))
        goto done;
    while (!tired_manager_probe_step(probe))
        if (!wait_bus(native->bus, error))
            goto done;
    TiredManagerProbeResult version = tired_manager_probe_result(probe);
    if (version.error.status != TIRED_OK)
    {
        *error = version.error;
        goto done;
    }
    if (!tired_load_paths_start(native->identity, 5000, &paths, error))
        goto done;
    while (!tired_load_paths_step(paths))
        if (!wait_bus(native->bus, error))
            goto done;
    TiredLoadPathsResult directories = tired_load_paths_result(paths);
    if (directories.error.status != TIRED_OK)
    {
        *error = directories.error;
        goto done;
    }
    for (size_t i = 0; i < directories.directories->count; ++i)
    {
        const TiredText *path = &directories.directories->items[i];
        if (!tired_text_list_append(&native->paths, path->data, path->length, 256,
                                    TIRED_INPUT_LIMIT, error))
            goto done;
    }
    if (!tired_layout_check_unit_path(layout, &native->paths, error))
        goto done;
    *backend = (TiredBackend){.context = native,
                              .load_paths = &native->paths,
                              .features = {.systemd_version = version.major_version},
                              .query = query,
                              .reload = reload,
                              .enable = enable,
                              .job = job,
                              .reset_failed = reset_failed,
                              .tick = progress,
                              .effects = effects,
                              .execution_possible = execution_possible,
                              .verify = verify,
                              .linger = linger};
    backend->features.features[0] =
        TIRED_FACT_TRUE; /* LoadCredential is part of the supported baseline. */
    struct rlimit nofile;
    sd_bus_creds *credentials = NULL;
    pid_t manager_pid = 0;
    uid_t manager_uid = (uid_t)-1;
    (void)sd_bus_set_method_call_timeout(native->bus, 5000000);
    resource_features(native, backend);
    bool manager_process =
        sd_bus_get_name_creds(native->bus, identity.unique_name,
                              SD_BUS_CREDS_PID | SD_BUS_CREDS_EUID, &credentials) >= 0 &&
        sd_bus_creds_get_pid(credentials, &manager_pid) >= 0 && manager_pid > 0 &&
        sd_bus_creds_get_euid(credentials, &manager_uid) >= 0 && manager_uid == identity.uid;
    bool manager_limit = manager_process && prlimit(manager_pid, RLIMIT_NOFILE, NULL, &nofile) == 0;
    if (manager_limit && native->user)
    {
        backend->features.nofile_known = true;
        backend->features.nofile_ceiling =
            (TiredLimit){.value = nofile.rlim_max, .infinity = nofile.rlim_max == RLIM_INFINITY};
    }
    if (manager_process)
    {
        char filename[96];
        (void)snprintf(filename, sizeof(filename), "/proc/%ld/status", (long)manager_pid);
        TiredText status = {0};
        TiredError ignored = {0};
        if (tired_read_file(filename, 65536, &status, &ignored))
        {
            const char *permitted = strstr(status.data, "\nCapPrm:\t");
            const char *bounding = strstr(status.data, "\nCapBnd:\t");
            if (permitted != NULL && bounding != NULL)
            {
                char *end;
                errno = 0;
                unsigned long long bits = strtoull(permitted + 9, &end, 16);
                bool parsed = errno == 0 && end == permitted + 25 && *end == '\n';
                unsigned long long bounds = strtoull(bounding + 9, &end, 16);
                parsed = parsed && errno == 0 && end == bounding + 25 && *end == '\n';
                if (parsed)
                {
                    backend->capabilities_known = true;
                    backend->permitted_capabilities = (uint64_t)(bits & bounds);
                    if (backend->permitted_capabilities == 0)
                        backend->features.features[4] = TIRED_FACT_FALSE;
                }
            }
        }
        tired_text_destroy(&status);
        struct rlimit stack;
        if (prlimit(manager_pid, RLIMIT_STACK, NULL, &stack) == 0)
        {
            uint64_t allowance =
                stack.rlim_cur == RLIM_INFINITY ? 6 * 1024 * 1024 : stack.rlim_cur / 4;
            backend->argument_max = allowance < 131072            ? 131072
                                    : allowance > 6 * 1024 * 1024 ? 6 * 1024 * 1024
                                                                  : allowance;
        }
    }
    sd_bus_creds_unref(credentials);
    if (backend->capabilities_known && (backend->permitted_capabilities & (UINT64_C(1) << 23)) != 0)
    {
        backend->nice_known = true;
        backend->minimum_nice = -20;
    }
    else if (manager_process)
    {
        struct rlimit priority;
        errno = 0;
        int inherited = getpriority(PRIO_PROCESS, (id_t)manager_pid);
        if (errno == 0 && prlimit(manager_pid, RLIMIT_NICE, NULL, &priority) == 0)
        {
            int granted = priority.rlim_cur >= 40 ? -20 : 20 - (int)priority.rlim_cur;
            backend->nice_known = true;
            backend->minimum_nice = inherited < granted ? inherited : granted;
        }
    }
    if (!native->user)
    {
        TiredText ceiling = {0};
        TiredError ignored = {0};
        uint64_t value;
        if (tired_read_file("/proc/sys/fs/nr_open", 64, &ceiling, &ignored))
        {
            while (ceiling.length > 0 && ceiling.data[ceiling.length - 1] == '\n')
                --ceiling.length;
            if (tired_parse_u64(ceiling.data, ceiling.length, 1, UINT64_MAX, &value, &ignored))
            {
                backend->features.nofile_known = true;
                backend->features.nofile_ceiling = (TiredLimit){.value = value};
            }
        }
        tired_text_destroy(&ceiling);
    }
    if (!native->user && manager_limit && backend->capabilities_known &&
        (backend->permitted_capabilities & (UINT64_C(1) << 24)) == 0)
    {
        TiredLimit inherited = {.value = nofile.rlim_max,
                                .infinity = nofile.rlim_max == RLIM_INFINITY};
        if (!backend->features.nofile_known ||
            tired_limit_le(inherited, backend->features.nofile_ceiling))
        {
            backend->features.nofile_known = true;
            backend->features.nofile_ceiling = inherited;
        }
    }
    if (native->user)
        linger_status(backend);
    *output = native;
    native = NULL;
    ok = true;
done:
    tired_load_paths_destroy(paths);
    tired_manager_probe_destroy(probe);
    tired_backend_destroy(native);
    return ok;
}
void tired_backend_authorization(TiredNativeBackend *native, bool interactive)
{
    native->interactive = interactive;
}
void tired_backend_progress(TiredNativeBackend *native, bool enabled)
{
    native->progress = enabled;
    native->began = tired_monotonic_usec();
}
void tired_backend_destroy(TiredNativeBackend *native)
{
    if (native == NULL)
        return;
    TiredManagerIdentityResult owner = native->identity == NULL
                                           ? (TiredManagerIdentityResult){0}
                                           : tired_manager_identity_result(native->identity);
    for (size_t i = 0; owner.ready && i < native->pinned.count; ++i)
        (void)sd_bus_call_method(native->bus, owner.unique_name, "/org/freedesktop/systemd1",
                                 "org.freedesktop.systemd1.Manager", "UnrefUnit", NULL, NULL, "s",
                                 native->pinned.items[i].data);
    tired_text_list_destroy(&native->pinned);
    tired_text_list_destroy(&native->paths);
    tired_manager_identity_destroy(native->identity);
    sd_bus_close_unref(native->bus);
    free(native);
}
