#include "inspection_live.h"
#include "tired/encode.h"
#include "tired/status_frontend.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static bool text(TiredBuffer *buffer, const char *value, TiredError *error)
{
    return tired_buffer_append(buffer, value, strlen(value), error);
}
static bool line(TiredBuffer *buffer, const char *label, const char *value, TiredError *error)
{
    TiredText safe = {0};
    bool ok = tired_encode_display(value, strlen(value), &safe, error) &&
              text(buffer, label, error) && text(buffer, ": ", error) &&
              text(buffer, safe.data, error) && text(buffer, "\n", error);
    tired_text_destroy(&safe);
    return ok;
}
static const char *const states[] = {"unknown", "not_required", "missing", "drifted", "match"};
static struct json_object *file_json(const TiredServiceFile *file, bool unit)
{
    struct json_object *object = json_object_new_object();
    if (object == NULL)
        return NULL;
    bool ok = add(object, "state", json_object_new_string(states[file->state]));
    if (ok &&
        (file->state == TIRED_SERVICE_FILE_MATCH || file->state == TIRED_SERVICE_FILE_DRIFTED))
        ok =
            add(object, "owner_matches", json_object_new_boolean(file->owner_matches)) &&
            add(object, "mode_matches", json_object_new_boolean(file->mode_matches)) &&
            add(object, "digest_matches", json_object_new_boolean(file->digest_matches)) &&
            (!unit || add(object, "marker_matches", json_object_new_boolean(file->marker_matches)));
    if (ok && file->error.code != NULL)
        ok = add(object, "error", json_object_new_string(file->error.code));
    if (ok)
        return object;
    json_object_put(object);
    return NULL;
}
bool tired_status_output(const TiredStatusView *view, bool json, bool check_active,
                         TiredText *output, TiredStatus *result, TiredError *error)
{
    assert(view != NULL && view->unit_name != NULL && output != NULL && result != NULL);
    const TiredUnitQueryResult *query = &view->live.query;
    bool known = query->done && query->error.status == TIRED_OK;
    const TiredUnitObservation *observed = known ? query->observation : NULL;
    const TiredObservedValue *active =
        observed == NULL ? NULL : &observed->fields[TIRED_OBS_ACTIVE_STATE];
    bool running =
        active != NULL && active->known && strcmp(active->value.text.data, "active") == 0;
    const char *fragment = "unknown";
    if (view->record != NULL && observed != NULL && observed->fields[TIRED_OBS_FRAGMENT_PATH].known)
    {
        const char *path = observed->fields[TIRED_OBS_FRAGMENT_PATH].value.text.data;
        fragment = path[0] == '\0'                                   ? "missing"
                   : strcmp(path, view->record->unit_path.data) == 0 ? "match"
                                                                     : "different";
    }
    TiredStatus status =
        !known ? (query->error.status == TIRED_OK ? TIRED_RUNTIME_FAILED : query->error.status)
        : view->record == NULL && view->record_error.status == TIRED_NOT_FOUND &&
                !query->file_found && !query->object_found
            ? TIRED_NOT_FOUND
        : check_active && !running ? TIRED_RUNTIME_FAILED
                                   : TIRED_OK;
    const char *record_state = view->record != NULL                           ? "present"
                               : view->record_error.status == TIRED_NOT_FOUND ? "missing"
                                                                              : "unknown";
    const char *transaction = view->transactions_error.status != TIRED_OK ? "unknown"
                              : view->transaction_pending                 ? "pending"
                                                                          : "none";
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    struct json_object *document = NULL;
    bool ok = false;
    if (json)
    {
        document = json_object_new_object();
        if (document == NULL || !add(document, "schema_version", json_object_new_int(1)) ||
            !add(document, "command", json_object_new_string("status")) ||
            !add(document, "unit_name", json_object_new_string(view->unit_name)) ||
            !add(document, "scope", json_object_new_string(view->user_scope ? "user" : "system")) ||
            !add(document, "ok", json_object_new_boolean(status == TIRED_OK)) ||
            !add(document, "exit_code", json_object_new_int(status)) ||
            !add(document, "record", json_object_new_string(record_state)) ||
            !add(document, "transactions", json_object_new_string(transaction)) ||
            !add(document, "fragment", json_object_new_string(fragment)) ||
            !add(document, "live", tired_inspection_live_json(&view->live)))
            goto allocation;
        if (view->record != NULL)
        {
            if (!add(document, "service_uuid",
                     json_object_new_string(view->record->metadata.service_uuid)) ||
                !add(document, "service_uid",
                     json_object_new_uint64(view->record->metadata.service_uid)) ||
                !add(document, "unit_file", file_json(&view->files.unit, true)) ||
                !add(document, "environment_file", file_json(&view->files.environment, false)))
                goto allocation;
        }
        if ((view->record_error.code != NULL &&
             !add(document, "record_error", json_object_new_string(view->record_error.code))) ||
            (view->transactions_error.code != NULL &&
             !add(document, "transactions_error",
                  json_object_new_string(view->transactions_error.code))))
            goto allocation;
        const char *encoded = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PRETTY);
        if (encoded == NULL)
            goto allocation;
        if (!text(&buffer, encoded, error) || !text(&buffer, "\n", error))
            goto done;
    }
    else
    {
        if (!line(&buffer, "Unit", view->unit_name, error) ||
            !line(&buffer, "Scope", view->user_scope ? "user" : "system", error) ||
            !line(&buffer, "Record", record_state, error) ||
            !line(&buffer, "Transactions", transaction, error) ||
            !line(&buffer, "Fragment agreement", fragment, error))
            goto done;
        if (view->record != NULL &&
            (!line(&buffer, "Service UUID", view->record->metadata.service_uuid, error) ||
             !line(&buffer, "Unit file", states[view->files.unit.state], error) ||
             !line(&buffer, "Owned environment", states[view->files.environment.state], error)))
            goto done;
        const TiredError *errors[] = {&view->record_error, &view->transactions_error,
                                      &view->files.unit.error, &view->files.environment.error};
        for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i)
            if (errors[i]->message != NULL && !line(&buffer, "Detail", errors[i]->message, error))
                goto done;
        if (!known)
        {
            if (!line(&buffer, "Live state",
                      query->error.message == NULL ? "unknown" : query->error.message, error))
                goto done;
        }
        else
        {
            if (!line(&buffer, "Unit-file state",
                      query->file_state == NULL ? "not found" : query->file_state, error) ||
                !line(&buffer, "Loaded object", query->object_found ? "present" : "not loaded",
                      error))
                goto done;
            for (size_t i = 0; i < TIRED_OBS_COUNT; ++i)
            {
                const TiredObservationField *field = tired_observation_field((TiredObservationId)i);
                const TiredObservedValue *value = observed == NULL ? NULL : &observed->fields[i];
                const char *display = "unknown";
                char number[32];
                if (value != NULL && value->known)
                {
                    if (field->type == TIRED_OBS_TEXT_LIST)
                    {
                        if (value->value.list.count == 0 &&
                            !line(&buffer, field->property, "none", error))
                            goto done;
                        for (size_t j = 0; j < value->value.list.count; ++j)
                            if (!line(&buffer, field->property, value->value.list.items[j].data,
                                      error))
                                goto done;
                        continue;
                    }
                    if (field->type == TIRED_OBS_TEXT)
                        display = value->value.text.data;
                    else
                    {
                        if (field->type == TIRED_OBS_I32)
                            (void)snprintf(number, sizeof(number), "%" PRId64,
                                           value->value.signed_value);
                        else
                            (void)snprintf(number, sizeof(number), "%" PRIu64,
                                           value->value.unsigned_value);
                        display = number;
                    }
                }
                if (!line(&buffer, field->property, display, error))
                    goto done;
            }
        }
    }
    if (!tired_buffer_take(&buffer, output, error))
        goto done;
    *result = status;
    tired_error_clear(error);
    ok = true;
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate status output.", 0);
done:
    json_object_put(document);
    tired_buffer_destroy(&buffer);
    return ok;
}
