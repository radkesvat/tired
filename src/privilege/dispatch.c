#include "tired/helper.h"
#include "tired/io.h"
#include "tired/json.h"
#include <string.h>
#include <unistd.h>
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
bool tired_helper_error_output(const TiredError *failure, TiredText *output, TiredError *error)
{
    struct json_object *document = json_object_new_object();
    bool ok =
        document != NULL && add(document, "schema_version", json_object_new_int(1)) &&
        add(document, "ok", json_object_new_boolean(false)) &&
        add(document, "exit_code",
            json_object_new_int(failure->status == TIRED_OK ? TIRED_INTERNAL : failure->status)) &&
        add(document, "error",
            json_object_new_string(failure->code == NULL ? "worker-failed" : failure->code)) &&
        add(document, "message",
            json_object_new_string(failure->message == NULL ? "Operation failed."
                                                            : failure->message));
    const char *bytes =
        ok ? json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN) : NULL;
    ok = bytes != NULL && tired_text_set(output, bytes, strlen(bytes), TIRED_INPUT_LIMIT, error);
    json_object_put(document);
    return ok;
}
bool tired_helper_dispatch(const TiredText *request, bool user_scope, bool interactive,
                           uid_t actor_uid, TiredText *output, TiredError *error)
{
    struct json_object *document = NULL, *kind = NULL, *scope = NULL, *uuid = NULL, *finish = NULL,
                       *version = NULL;
    TiredMutation mutation = {0};
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredOperationResult result = {0};
    bool ok = false, recovery = false;
    uint64_t schema;
    if (!tired_json_parse(request->data, request->length, TIRED_INPUT_LIMIT, &document, error))
        goto done;
    recovery = json_object_object_get_ex(document, "kind", &kind);
    bool record_request = recovery && json_object_is_type(kind, json_type_string) &&
                          strcmp(json_object_get_string(kind), "service_record") == 0;
    if (record_request)
    {
        struct json_object *name = NULL, *hydrate = NULL;
        TiredText encoded = {0};
        struct json_object *record = NULL, *reply = NULL;
        if (user_scope || json_object_object_length(document) != 4 ||
            !json_object_object_get_ex(document, "protocol_version", &version) ||
            !tired_json_u64(version, 1, 1, &schema, error) ||
            !json_object_object_get_ex(document, "unit_name", &name) ||
            !json_object_is_type(name, json_type_string) ||
            !json_object_object_get_ex(document, "hydrate", &hydrate) ||
            !json_object_is_type(hydrate, json_type_boolean))
            goto invalid;
        ok = tired_helper_record(json_object_get_string(name), json_object_get_boolean(hydrate),
                                 actor_uid, &encoded, error) &&
             tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &record, error);
        if (ok)
        {
            reply = json_object_new_object();
            ok = reply != NULL && add(reply, "schema_version", json_object_new_int(1)) &&
                 add(reply, "ok", json_object_new_boolean(true)) &&
                 add(reply, "exit_code", json_object_new_int(0));
            if (ok)
            {
                ok = add(reply, "record", record);
                record = NULL;
            }
            const char *bytes =
                ok ? json_object_to_json_string_ext(reply, JSON_C_TO_STRING_PLAIN) : NULL;
            ok = bytes != NULL &&
                 tired_text_set(output, bytes, strlen(bytes), TIRED_INPUT_LIMIT, error);
        }
        json_object_put(reply);
        json_object_put(record);
        tired_text_destroy(&encoded);
        goto done;
    }
    bool account_change = recovery && json_object_is_type(kind, json_type_string) &&
                          strcmp(json_object_get_string(kind), "enable_linger") == 0;
    uint64_t account_uid = 0;
    if (account_change)
    {
        struct json_object *uid = NULL;
        if (user_scope || json_object_object_length(document) != 3 ||
            !json_object_object_get_ex(document, "protocol_version", &version) ||
            !tired_json_u64(version, 1, 1, &schema, error) ||
            !json_object_object_get_ex(document, "uid", &uid) ||
            !tired_json_u64(uid, 0, UINT32_MAX - 1, &account_uid, error) ||
            account_uid != actor_uid)
            goto invalid;
    }
    else if (recovery)
    {
        if (json_object_object_length(document) != 5 ||
            !json_object_is_type(kind, json_type_string) ||
            strcmp(json_object_get_string(kind), "recover") != 0 ||
            !json_object_object_get_ex(document, "protocol_version", &version) ||
            !tired_json_u64(version, 1, 1, &schema, error) ||
            !json_object_object_get_ex(document, "user_scope", &scope) ||
            !json_object_is_type(scope, json_type_boolean) ||
            (bool)json_object_get_boolean(scope) != user_scope ||
            !json_object_object_get_ex(document, "transaction_uuid", &uuid) ||
            !json_object_is_type(uuid, json_type_string) ||
            !tired_uuid_valid(json_object_get_string(uuid),
                              (size_t)json_object_get_string_len(uuid)) ||
            !json_object_object_get_ex(document, "finish", &finish) ||
            !json_object_is_type(finish, json_type_boolean))
            goto invalid;
    }
    else if (!tired_mutation_parse(request->data, request->length, &mutation, error))
        goto done;
    else if (mutation.proposed.metadata.user_scope != user_scope)
        goto invalid;
    if (!recovery && mutation.actor_uid != actor_uid)
    {
        tired_error_set(error, TIRED_AUTHORIZATION, "invocation-origin",
                        "The operation identity differs from the authorized invoking identity.", 0);
        goto done;
    }
    if (!recovery && mutation.operation == TIRED_TRANSACTION_CREATE &&
        mutation.proposed.metadata.invoking_uid != actor_uid)
    {
        tired_error_set(error, TIRED_AUTHORIZATION, "invocation-origin",
                        "The captured invoking identity does not match the authorized invocation.",
                        0);
        goto done;
    }
    bool executing =
        !recovery && (mutation.operation == TIRED_TRANSACTION_CREATE ||
                      mutation.operation == TIRED_TRANSACTION_EDIT ||
                      mutation.operation == TIRED_TRANSACTION_RENAME ||
                      mutation.operation == TIRED_TRANSACTION_RESTORE ||
                      mutation.operation == TIRED_TRANSACTION_START ||
                      mutation.operation == TIRED_TRANSACTION_RESTART ||
                      (mutation.operation == TIRED_TRANSACTION_ENABLE && mutation.now));
    if (executing && mutation.operation == TIRED_TRANSACTION_CREATE && actor_uid != 0 &&
        mutation.proposed.metadata.service_uid == 0 &&
        !mutation.proposed.review.acknowledged[TIRED_RISK_ROOT])
    {
        tired_error_set(
            error, TIRED_INVALID, "run-as-root",
            "Root execution from this invocation requires explicit run-as-root acknowledgment.", 0);
        goto done;
    }
    if (!tired_layout_discover(user_scope, &layout, error) ||
        !tired_backend_open(&layout, &native, &backend, error))
        goto done;
    tired_backend_authorization(native, interactive);
    tired_backend_progress(
        native, interactive ||
                    (!recovery && mutation.proposed.metadata.interactive && isatty(STDERR_FILENO)));
    if (account_change)
    {
        bool uncertain = false;
        ok = backend.linger(backend.context, (uid_t)account_uid, &uncertain, error);
        if (!ok && uncertain)
            error->status = TIRED_RECOVERY_REQUIRED;
        if (ok)
        {
            const char reply[] = "{\"schema_version\":1,\"ok\":true,\"exit_code\":0,\"account_"
                                 "change\":\"linger_enabled\"}";
            ok = tired_text_set(output, reply, sizeof(reply) - 1, TIRED_INPUT_LIMIT, error);
        }
        goto done;
    }
    ok = recovery ? tired_mutation_recover(&layout, &backend, json_object_get_string(uuid),
                                           json_object_get_boolean(finish), &result, error)
         : mutation.recovery ? tired_mutation_recover(&layout, &backend,
                                                      mutation.proposed.metadata.transaction_uuid,
                                                      mutation.finish, &result, error)
                             : tired_mutation_apply(&mutation, &layout, &backend, &result, error);
    if (ok)
        ok = tired_operation_output(&result, recovery ? "recover" : "operation", true, output,
                                    error);
    goto done;
invalid:
    tired_error_set(error, TIRED_INVALID, "helper-protocol",
                    "Invalid recovery request or worker scope.", 0);
done:
    tired_backend_destroy(native);
    tired_layout_destroy(&layout);
    tired_mutation_destroy(&mutation);
    tired_operation_result_destroy(&result);
    json_object_put(document);
    return ok;
}
