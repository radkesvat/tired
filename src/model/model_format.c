#include "tired/model_format.h"
#include "tired/json.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static const char *const origins[] = {"unset",       "inherited", "default", "administrator-config",
                                      "user-config", "profile",   "user",    "capture"};
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "model-snapshot", "Invalid typed model snapshot.",
                           0);
}
bool tired_spec_parse(const char *data, size_t length, TiredServiceSpec *output, TiredError *error)
{
    assert(output != NULL);
    struct json_object *document = NULL, *version = NULL, *fields = NULL;
    TiredServiceSpec spec = {0};
    bool ok = false;
    uint64_t schema;
    if (!tired_json_parse(data, length, TIRED_INPUT_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) != 2 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "fields", &fields) ||
        !json_object_is_type(fields, json_type_object) ||
        json_object_object_length(fields) != TIRED_FIELD_COUNT)
        goto bad;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
    {
        const TiredField *field = tired_field_get((TiredFieldId)i);
        struct json_object *entry = NULL, *origin = NULL, *inherit = NULL, *value = NULL;
        if (!json_object_object_get_ex(fields, field->name, &entry) ||
            !json_object_is_type(entry, json_type_object) ||
            json_object_object_length(entry) != 3 ||
            !json_object_object_get_ex(entry, "origin", &origin) ||
            !json_object_is_type(origin, json_type_string) ||
            !json_object_object_get_ex(entry, "inherit", &inherit) ||
            !json_object_is_type(inherit, json_type_boolean) ||
            !json_object_object_get_ex(entry, "value", &value))
            goto bad;
        unsigned selected;
        for (selected = 0; selected <= TIRED_ORIGIN_CAPTURE; ++selected)
            if (strcmp(json_object_get_string(origin), origins[selected]) == 0)
                break;
        if (selected > TIRED_ORIGIN_CAPTURE)
            goto bad;
        TiredFieldOrigin source = (TiredFieldOrigin)selected;
        if (json_object_get_boolean(inherit))
        {
            if (source != TIRED_ORIGIN_USER || value != NULL ||
                !tired_spec_inherit(&spec, field->id, error))
                goto bad;
        }
        else if (source <= TIRED_ORIGIN_INHERITED)
        {
            if (value != NULL)
                goto bad;
            spec.fields[i].origin = source;
        }
        else if (field->kind == TIRED_FIELD_LIST)
        {
            if (!json_object_is_type(value, json_type_array) ||
                !tired_spec_clear_list(&spec, field->id, source, error))
                goto bad;
            for (size_t j = 0; j < json_object_array_length(value); ++j)
            {
                struct json_object *item = json_object_array_get_idx(value, j);
                if (!json_object_is_type(item, json_type_string))
                    goto bad;
                if (!tired_spec_append(&spec, field->id, json_object_get_string(item),
                                       (size_t)json_object_get_string_len(item), source, error))
                    goto done;
            }
        }
        else
        {
            if (!json_object_is_type(value, json_type_string))
                goto bad;
            /* Stored names are already normalized; do not strip another suffix. */
            if (!tired_spec_set(&spec, field->id, json_object_get_string(value),
                                (size_t)json_object_get_string_len(value),
                                field->id == TIRED_FIELD_NAME ? TIRED_ORIGIN_CAPTURE : source, true,
                                error))
                goto done;
            spec.fields[i].origin = source;
        }
    }
    if (!tired_spec_validate_scalars(&spec, error))
        goto done;
    tired_spec_destroy(output);
    *output = spec;
    spec = (TiredServiceSpec){0};
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    json_object_put(document);
    tired_spec_destroy(&spec);
    return ok;
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
static struct json_object *scalar(const TiredField *field, const TiredFieldValue *value)
{
    char buffer[96];
    switch (field->kind)
    {
    case TIRED_FIELD_TEXT:
        return json_object_new_string_len(value->value.text.data, (int)value->value.text.length);
    case TIRED_FIELD_CHOICE:
    {
        const char *start = field->choices;
        for (size_t i = 0; i < value->value.choice; ++i)
        {
            start = strchr(start, '|');
            if (start == NULL)
                return NULL;
            ++start;
        }
        const char *end = strchr(start, '|');
        return json_object_new_string_len(
            start, (int)(end == NULL ? strlen(start) : (size_t)(end - start)));
    }
    case TIRED_FIELD_BOOL:
        return json_object_new_string(value->value.boolean ? "true" : "false");
    case TIRED_FIELD_INTEGER:
        (void)snprintf(buffer, sizeof(buffer), "%" PRId64, value->value.integer);
        break;
    case TIRED_FIELD_DURATION:
        (void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "us", value->value.microseconds);
        break;
    case TIRED_FIELD_TIMEOUT:
        if (value->value.timeout.infinity)
            return json_object_new_string("infinity");
        (void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "us", value->value.timeout.value);
        break;
    case TIRED_FIELD_LIMIT:
        if (value->value.limit.infinity)
            return json_object_new_string("infinity");
        (void)snprintf(buffer, sizeof(buffer), "%" PRIu64, value->value.limit.value);
        break;
    case TIRED_FIELD_MODE:
        (void)snprintf(buffer, sizeof(buffer), "%04o", value->value.mode);
        break;
    case TIRED_FIELD_QUOTA:
        (void)snprintf(buffer, sizeof(buffer), "%" PRIu64 ".%02" PRIu64 "%%",
                       value->value.quota / 100, value->value.quota % 100);
        break;
    case TIRED_FIELD_SIGNAL:
        (void)snprintf(buffer, sizeof(buffer), "%d", value->value.signal_number);
        break;
    case TIRED_FIELD_LIST:
        return NULL;
    }
    return json_object_new_string(buffer);
}
bool tired_spec_encode(const TiredServiceSpec *spec, TiredText *output, TiredError *error)
{
    assert(spec != NULL && output != NULL);
    struct json_object *document = json_object_new_object(), *fields = json_object_new_object();
    struct json_object *entry = NULL, *value = NULL;
    TiredServiceSpec validated = {0};
    bool ok = false;
    if (document == NULL || fields == NULL)
        goto allocation;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
    {
        const TiredField *field = tired_field_get((TiredFieldId)i);
        const TiredFieldValue *source = &spec->fields[i];
        if ((unsigned)source->origin > TIRED_ORIGIN_CAPTURE)
        {
            invalid(error);
            goto done;
        }
        entry = json_object_new_object();
        if (entry == NULL ||
            !add(entry, "origin", json_object_new_string(origins[source->origin])) ||
            !add(entry, "inherit", json_object_new_boolean(source->inherit)))
            goto allocation;
        if (tired_field_has_value(source))
        {
            if (field->kind == TIRED_FIELD_LIST)
            {
                value = json_object_new_array();
                if (value == NULL)
                    goto allocation;
                for (size_t j = 0; j < source->value.list.count; ++j)
                {
                    const TiredText *item = &source->value.list.items[j];
                    struct json_object *text =
                        json_object_new_string_len(item->data, (int)item->length);
                    if (text == NULL || json_object_array_add(value, text) != 0)
                    {
                        json_object_put(text);
                        goto allocation;
                    }
                }
            }
            else
                value = scalar(field, source);
            bool inserted = add(entry, "value", value);
            value = NULL;
            if (!inserted)
                goto allocation;
        }
        else if (json_object_object_add(entry, "value", NULL) != 0)
            goto allocation;
        bool inserted = add(fields, field->name, entry);
        entry = NULL;
        if (!inserted)
            goto allocation;
    }
    bool inserted = add(document, "fields", fields);
    fields = NULL;
    if (!inserted || !add(document, "schema_version", json_object_new_int(1)))
        goto allocation;
    const char *bytes = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        goto allocation;
    if (!tired_spec_parse(bytes, strlen(bytes), &validated, error))
        goto done;
    ok = tired_text_set(output, bytes, strlen(bytes), TIRED_INPUT_LIMIT, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode typed model snapshot.", 0);
done:
    tired_spec_destroy(&validated);
    json_object_put(value);
    json_object_put(entry);
    json_object_put(fields);
    json_object_put(document);
    return ok;
}
