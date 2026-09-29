#include "tired/journal_output.h"
#include "tired/encode.h"
#include "tired/journal_selection.h"
#include <assert.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

static bool append(TiredBuffer *buffer, const char *value, TiredError *error)
{
    return tired_buffer_append(buffer, value, strlen(value), error);
}
static bool escaped(TiredBuffer *buffer, const unsigned char *data, size_t length,
                    TiredError *error)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = data[i];
        if (c >= 32 && c <= 126 && c != '\\')
        {
            if (!tired_buffer_append(buffer, (const char *)&data[i], 1, error))
                return false;
        }
        else if (c == '\\')
        {
            if (!append(buffer, "\\\\", error))
                return false;
        }
        else
        {
            char value[] = {'\\', 'x', hex[c >> 4], hex[c & 15]};
            if (!tired_buffer_append(buffer, value, sizeof(value), error))
                return false;
        }
    }
    return true;
}
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static struct json_object *bytes(const TiredJournalBytes *value)
{
    struct json_object *object = json_object_new_object();
    TiredBuffer hex;
    TiredError error = {0};
    tired_buffer_init(&hex, TIRED_JOURNAL_MESSAGE_LIMIT * 2U);
    static const char digits[] = "0123456789abcdef";
    if (object == NULL || !add(object, "present", json_object_new_boolean(value->present)))
        goto fail;
    if (value->present)
    {
        for (size_t i = 0; i < value->length; ++i)
        {
            char pair[] = {digits[value->data[i] >> 4], digits[value->data[i] & 15]};
            if (!tired_buffer_append(&hex, pair, 2, &error))
                goto fail;
        }
        if (!add(object, "encoding", json_object_new_string("hex")) ||
            !add(object, "data",
                 json_object_new_string_len(hex.data == NULL ? "" : hex.data, (int)hex.length)) ||
            !add(object, "length", json_object_new_uint64(value->length)) ||
            !add(object, "truncated", json_object_new_boolean(value->truncated)))
            goto fail;
    }
    tired_buffer_destroy(&hex);
    return object;
fail:
    tired_buffer_destroy(&hex);
    json_object_put(object);
    return NULL;
}
static struct json_object *number(const TiredJournalNumber *value)
{
    struct json_object *object = json_object_new_object();
    if (object != NULL && add(object, "known", json_object_new_boolean(value->known)) &&
        add(object, "invalid", json_object_new_boolean(value->invalid)) &&
        (!value->known || add(object, "value", json_object_new_uint64(value->value))))
        return object;
    json_object_put(object);
    return NULL;
}
bool tired_journal_output(const TiredJournalRecord *record, const char *unit, bool user_scope,
                          uid_t uid, bool json, TiredText *output, TiredError *error)
{
    assert(record != NULL && unit != NULL && output != NULL);
    assert(record->message.length <= TIRED_JOURNAL_MESSAGE_LIMIT &&
           record->identifier.length <= 256);
    TiredJournalSelection selection;
    if (!tired_journal_selection_build(unit, user_scope, uid, NULL, &selection, error))
        return false;
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 512U * 1024U);
    struct json_object *object = NULL;
    bool ok = false;
    if (json)
    {
        object = json_object_new_object();
        if (object == NULL || !add(object, "schema_version", json_object_new_int(1)) ||
            !add(object, "event_type", json_object_new_string("journal_record")) ||
            !add(object, "selected_service", json_object_new_string(unit)) ||
            !add(object, "scope", json_object_new_string(user_scope ? "user" : "system")) ||
            (user_scope && !add(object, "selected_uid", json_object_new_uint64(uid))) ||
            !add(object, "cursor",
                 json_object_new_string_len(record->cursor.data, (int)record->cursor.length)) ||
            !add(object, "realtime_usec", json_object_new_uint64(record->realtime_usec)) ||
            !add(object, "monotonic_usec", json_object_new_uint64(record->monotonic_usec)) ||
            !add(object, "boot_id", json_object_new_string(record->boot_id)) ||
            !add(object, "message", bytes(&record->message)) ||
            !add(object, "identifier", bytes(&record->identifier)) ||
            !add(object, "pid", number(&record->pid)) ||
            !add(object, "uid", number(&record->uid)) ||
            !add(object, "priority", number(&record->priority)))
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot construct journal event.",
                            0);
            goto done;
        }
        const char *encoded = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
        if (encoded == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot serialize journal event.",
                            0);
            goto done;
        }
        if (!append(&buffer, encoded, error))
            goto done;
    }
    else
    {
        char prefix[320];
        (void)snprintf(prefix, sizeof(prefix), "@%" PRIu64 ".%06" PRIu64 " %s %s",
                       record->realtime_usec / 1000000U, record->realtime_usec % 1000000U,
                       user_scope ? "user" : "system", unit);
        if (!append(&buffer, prefix, error))
            goto done;
        if (user_scope)
        {
            (void)snprintf(prefix, sizeof(prefix), " uid=%" PRIuMAX, (uintmax_t)uid);
            if (!append(&buffer, prefix, error))
                goto done;
        }
        if (record->identifier.present)
        {
            if (!append(&buffer, " [", error) ||
                !escaped(&buffer, record->identifier.data, record->identifier.length, error) ||
                (record->identifier.truncated && !append(&buffer, " [truncated]", error)) ||
                !append(&buffer, "]", error))
                goto done;
        }
        if (!append(&buffer, ": ", error))
            goto done;
        if (record->message.present)
        {
            if (!escaped(&buffer, record->message.data, record->message.length, error) ||
                (record->message.truncated && !append(&buffer, " [truncated]", error)))
                goto done;
        }
        else if (!append(&buffer, "[message absent]", error))
            goto done;
    }
    ok = append(&buffer, "\n", error) && tired_buffer_take(&buffer, output, error);
done:
    json_object_put(object);
    tired_buffer_destroy(&buffer);
    return ok;
}
