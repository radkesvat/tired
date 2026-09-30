#include "tired/encode.h"
#include "tired/mutation.h"
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
static bool put(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return json_object_object_add(object, key, NULL) == 0;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static bool line(TiredBuffer *buffer, const char *label, const char *value, TiredError *error)
{
    TiredText safe = {0};
    bool ok = tired_encode_display(value, strlen(value), &safe, error) &&
              tired_buffer_append(buffer, label, strlen(label), error) &&
              tired_buffer_append(buffer, safe.data, safe.length, error) &&
              tired_buffer_append(buffer, "\n", 1, error);
    tired_text_destroy(&safe);
    return ok;
}
bool tired_operation_output(const TiredOperationResult *result, const char *command, bool json,
                            TiredText *output, TiredError *error)
{
    if (json)
    {
        struct json_object *root = json_object_new_object(), *service = json_object_new_object(),
                           *installation = json_object_new_object(),
                           *runtime = json_object_new_object();
        bool ok = root != NULL && service != NULL && installation != NULL && runtime != NULL;
        struct json_object *warnings = json_object_new_array(), *errors = json_object_new_array();
        ok = ok && warnings != NULL && errors != NULL;
        if (ok && result->drift_backup)
            ok = json_object_array_add(
                     warnings, json_object_new_string("Foreign unit bytes are backed up in the "
                                                      "previous private history revision.")) == 0;
        if (ok && (strcmp(command, "stop") == 0 || strcmp(command, "remove") == 0 ||
                   strcmp(command, "rename") == 0 || strcmp(command, "restart") == 0))
            ok = json_object_array_add(warnings, json_object_new_string(
                                                     "Stopping a networking service may disconnect "
                                                     "the SSH route used to operate it.")) == 0;
        if (ok && result->leaves_children)
            ok = json_object_array_add(
                     warnings,
                     json_object_new_string(
                         "KillMode may leave child processes running after stop or removal.")) == 0;
        const TiredError *failures[] = {&result->original_error, &result->error};
        for (size_t i = 0; ok && i < 2; ++i)
        {
            const TiredError *failure = failures[i];
            if (failure->status == TIRED_OK || (i == 1 && failure->status == failures[0]->status &&
                                                failure->code == failures[0]->code))
                continue;
            struct json_object *entry = json_object_new_object();
            ok = entry != NULL && put(entry, "exit_code", json_object_new_int(failure->status)) &&
                 put(entry, "code",
                     json_object_new_string(failure->code == NULL ? "unknown" : failure->code)) &&
                 put(entry, "message",
                     json_object_new_string(failure->message == NULL ? "Operation failed."
                                                                     : failure->message));
            if (ok)
                ok = json_object_array_add(errors, entry) == 0;
            if (!ok)
                json_object_put(entry);
        }
        if (ok)
        {
            ok = put(root, "warnings", warnings);
            warnings = NULL;
        }
        if (ok)
        {
            ok = put(root, "errors", errors);
            errors = NULL;
        }
        ok =
            ok && put(root, "schema_version", json_object_new_int(1)) &&
            put(root, "command", json_object_new_string(command)) &&
            put(root, "ok", json_object_new_boolean(result->status == TIRED_OK)) &&
            put(root, "exit_code", json_object_new_int(result->status)) &&
            put(root, "outcome",
                json_object_new_string(result->outcome == NULL ? "unknown" : result->outcome)) &&
            put(root, "transaction", json_object_new_string(result->transaction_uuid)) &&
            put(service, "id", json_object_new_string(result->service_uuid)) &&
            put(service, "unit",
                json_object_new_string(result->unit_name.data == NULL ? "unknown"
                                                                      : result->unit_name.data)) &&
            put(service, "unit_path",
                json_object_new_string(result->unit_path.data == NULL ? "unknown"
                                                                      : result->unit_path.data)) &&
            put(service, "scope", json_object_new_string(result->user_scope ? "user" : "system")) &&
            put(service, "run_as",
                result->run_as.data == NULL ? NULL : json_object_new_string(result->run_as.data)) &&
            put(installation, "state",
                json_object_new_string(result->recovery_required ? "unknown"
                                       : result->rolled_back     ? "rolled_back"
                                       : result->installed       ? "committed"
                                                                 : "removed")) &&
            put(installation, "enabled",
                result->runtime.file_state.data == NULL
                    ? NULL
                    : json_object_new_boolean(result->runtime.enabled)) &&
            put(installation, "rollback", json_object_new_boolean(result->rolled_back)) &&
            put(installation, "recovery_required",
                json_object_new_boolean(result->recovery_required)) &&
            put(runtime, "active_state",
                result->runtime.active_state.data == NULL
                    ? NULL
                    : json_object_new_string(result->runtime.active_state.data)) &&
            put(runtime, "sub_state",
                result->runtime.sub_state.data == NULL
                    ? NULL
                    : json_object_new_string(result->runtime.sub_state.data)) &&
            put(runtime, "observation",
                json_object_new_string(result->runtime.running     ? "initial_process_running"
                                       : result->runtime.completed ? "completed"
                                                                   : "not_running_or_unknown")) &&
            put(runtime, "application_health", json_object_new_string("not_verified")) &&
            put(runtime, "earlier_start_context", json_object_new_boolean(result->deferred));
        const TiredError *cause =
            result->original_error.status != TIRED_OK ? &result->original_error : &result->error;
        if (ok && cause->status != TIRED_OK)
            ok = put(root, "error",
                     json_object_new_string(cause->code == NULL ? "internal" : cause->code)) &&
                 put(root, "message",
                     json_object_new_string(cause->message == NULL ? "Operation failed."
                                                                   : cause->message));
        if (ok)
        {
            ok = put(root, "service", service);
            service = NULL;
        }
        if (ok)
        {
            ok = put(root, "installation", installation);
            installation = NULL;
        }
        if (ok)
        {
            ok = put(root, "runtime", runtime);
            runtime = NULL;
        }
        const char *encoded =
            ok ? json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN) : NULL;
        TiredBuffer buffer;
        tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
        ok = encoded != NULL && tired_buffer_append(&buffer, encoded, strlen(encoded), error) &&
             tired_buffer_append(&buffer, "\n", 1, error) &&
             tired_buffer_take(&buffer, output, error);
        tired_buffer_destroy(&buffer);
        json_object_put(root);
        json_object_put(service);
        json_object_put(installation);
        json_object_put(runtime);
        json_object_put(warnings);
        json_object_put(errors);
        if (!ok && error->status == TIRED_OK)
            tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode operation result.",
                            0);
        return ok;
    }
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    const TiredError *cause =
        result->original_error.status != TIRED_OK ? &result->original_error : &result->error;
    bool ok =
        line(&buffer,
             "Service: ", result->unit_name.data == NULL ? "unknown" : result->unit_name.data,
             error) &&
        line(&buffer, "Outcome: ", result->outcome == NULL ? "unknown" : result->outcome, error) &&
        line(&buffer, "Unit: ", result->unit_path.data == NULL ? "unknown" : result->unit_path.data,
             error) &&
        line(&buffer, "Running now: ",
             result->runtime.active_state.data == NULL ? "unknown"
             : result->runtime.running                 ? "yes"
                                                       : "no",
             error) &&
        line(&buffer, "Enabled: ",
             result->runtime.file_state.data == NULL ? "unknown"
             : result->runtime.enabled               ? "yes"
                                                     : "no",
             error);
    if (ok && result->leaves_children)
        ok = line(&buffer,
                  "Warning: ", "KillMode may leave child processes running after stop or removal.",
                  error);
    if (ok && (strcmp(command, "stop") == 0 || strcmp(command, "remove") == 0 ||
               strcmp(command, "rename") == 0 || strcmp(command, "restart") == 0))
        ok = line(&buffer, "Warning: ",
                  "Stopping a networking service may disconnect the SSH route used to operate it.",
                  error);
    if (ok && cause->status != TIRED_OK)
        ok = line(&buffer,
                  "Diagnostic: ", cause->message == NULL ? "Operation failed." : cause->message,
                  error);
    if (ok && result->recovery_required)
        ok = line(&buffer, "Recovery transaction: ", result->transaction_uuid, error) &&
             line(&buffer,
                  "Next: ", "tired recover; inspect the selected transaction before resolution.",
                  error);
    if (ok && result->deferred)
        ok = line(&buffer, "Configuration: ",
                  "Changed; the running process still uses the earlier start context.", error);
    if (ok && result->installed && !result->recovery_required)
        ok =
            line(
                &buffer, "Supervisor: ",
                "systemd; tired does not need to stay running. Application health is not verified.",
                error) &&
            line(&buffer, "Next: ",
                 "tired status NAME; tired logs NAME --follow; tired edit NAME; tired remove NAME",
                 error);
    if (ok)
        ok = tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}
