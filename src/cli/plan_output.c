#include "tired/plan_output.h"
#include "tired/encode.h"
#include "tired/render.h"
#include <assert.h>
#include <json-c/json.h>
#include <string.h>

static bool secret_flag(const TiredText *arg)
{
    if (arg->length == 0 || arg->data[0] != '-')
        return false;
    char key[128];
    size_t i = 0;
    for (; i < arg->length && i < sizeof(key) - 1 && arg->data[i] != '='; ++i)
    {
        char c = arg->data[i];
        key[i] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
    }
    key[i] = '\0';
    return strstr(key, "password") != NULL || strstr(key, "passwd") != NULL ||
           strstr(key, "token") != NULL || strstr(key, "secret") != NULL ||
           strstr(key, "api-key") != NULL;
}
static bool add(struct json_object *object, const char *key, struct json_object *child)
{
    if (child == NULL)
        return false;
    if (json_object_object_add(object, key, child) != 0)
    {
        json_object_put(child);
        return false;
    }
    return true;
}
static bool push(struct json_object *array, struct json_object *child)
{
    if (child == NULL)
        return false;
    if (json_object_array_add(array, child) != 0)
    {
        json_object_put(child);
        return false;
    }
    return true;
}
static struct json_object *field_value(const TiredField *field, const TiredFieldValue *value)
{
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
        return json_object_new_boolean(value->value.boolean);
    case TIRED_FIELD_INTEGER:
        return json_object_new_int64(value->value.integer);
    case TIRED_FIELD_DURATION:
        return json_object_new_uint64(value->value.microseconds);
    case TIRED_FIELD_MODE:
        return json_object_new_int64(value->value.mode);
    case TIRED_FIELD_SIGNAL:
        return json_object_new_int(value->value.signal_number);
    case TIRED_FIELD_QUOTA:
        return json_object_new_uint64(value->value.quota);
    case TIRED_FIELD_LIMIT:
        return value->value.limit.infinity ? json_object_new_string("infinity")
                                           : json_object_new_uint64(value->value.limit.value);
    case TIRED_FIELD_LIST:
    {
        struct json_object *array = json_object_new_array();
        if (array == NULL)
            return NULL;
        for (size_t i = 0; i < value->value.list.count; ++i)
            if (!push(array, json_object_new_string_len(value->value.list.items[i].data,
                                                        (int)value->value.list.items[i].length)))
            {
                json_object_put(array);
                return NULL;
            }
        return array;
    }
    }
    return NULL;
}

bool tired_plan_output(const TiredPlan *plan, bool json, bool unit_only, bool include_sensitive,
                       TiredText *output, TiredError *error)
{
    assert(plan != NULL && output != NULL);
    TiredServiceSpec display = {0};
    TiredText unit = {0};
    TiredBuffer text;
    tired_buffer_init(&text, TIRED_UNIT_LIMIT * 4U);
    struct json_object *root = NULL, *fields = NULL, *environment = NULL, *warnings = NULL;
    bool redacted = false, sensitive = false;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (!tired_spec_copy_field(&display, &plan->spec, (TiredFieldId)i, error))
            goto fail;
    const TiredTextList *original = &plan->spec.fields[TIRED_FIELD_ARGV].value.list;
    if (!tired_spec_clear_list(&display, TIRED_FIELD_ARGV, TIRED_ORIGIN_CAPTURE, error))
        goto fail;
    bool hide_next = false;
    for (size_t i = 0; i < original->count; ++i)
    {
        const TiredText *arg = &original->items[i];
        bool flag = i != 0 && secret_flag(arg);
        bool attached = flag && memchr(arg->data, '=', arg->length) != NULL;
        bool hide = hide_next || attached;
        if (hide || flag)
            sensitive = true;
        const char *value = !include_sensitive && hide ? "[redacted]" : arg->data;
        size_t length = !include_sensitive && hide ? sizeof("[redacted]") - 1 : arg->length;
        if (!include_sensitive && hide)
            redacted = true;
        if (!tired_spec_append(&display, TIRED_FIELD_ARGV, value, length, TIRED_ORIGIN_CAPTURE,
                               error))
            goto fail;
        hide_next = flag && !attached;
    }
    if (!tired_render_unit(&display, plan->uuid,
                           plan->managed_environment.data == NULL ? NULL
                                                                  : &plan->managed_environment,
                           &plan->credentials, &unit, error))
        goto fail;
    if (json)
    {
        root = json_object_new_object();
        fields = json_object_new_object();
        environment = json_object_new_array();
        warnings = json_object_new_array();
        if (root == NULL || fields == NULL || environment == NULL || warnings == NULL)
            goto allocation;
        const char *origins[] = {"unset", "inherited", "default", "profile", "user", "capture"};
        for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        {
            const TiredField *field = tired_field_get((TiredFieldId)i);
            const TiredFieldValue *value = &display.fields[i];
            struct json_object *item = json_object_new_object();
            if (item == NULL)
                goto allocation;
            if (!add(item, "origin", json_object_new_string(origins[value->origin])) ||
                (value->origin >= TIRED_ORIGIN_DEFAULT &&
                 !add(item, "value", field_value(field, value))))
            {
                json_object_put(item);
                goto allocation;
            }
            if (!add(fields, field->name, item))
                goto allocation;
        }
        for (size_t i = 0; i < plan->environment.count; ++i)
        {
            const TiredEnvironmentEntry *entry = &plan->environment.items[i];
            struct json_object *item = json_object_new_object();
            if (item == NULL)
                goto allocation;
            if (!add(item, "name", json_object_new_string(entry->name.data)) ||
                !add(item, "value",
                     json_object_new_string(include_sensitive
                                                ? entry->value.data
                                                : tired_environment_display(entry))) ||
                !add(item, "sensitive", json_object_new_boolean(entry->sensitive)) ||
                !add(item, "origin", json_object_new_int(entry->origin)))
            {
                json_object_put(item);
                goto allocation;
            }
            if (!push(environment, item))
                goto allocation;
        }
        if (sensitive && !push(warnings, json_object_new_string("sensitive-command-data")))
            goto allocation;
        if (plan->service.uid == 0 && plan->invoking.uid != 0 &&
            !push(warnings, json_object_new_string("run-as-root")))
            goto allocation;
        if (plan->spec.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].origin >= TIRED_ORIGIN_DEFAULT &&
            plan->spec.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].value.list.count != 0 &&
            !push(warnings, json_object_new_string("privileged-capabilities")))
            goto allocation;
        if (tired_spec_choice_is(&plan->spec, TIRED_FIELD_RETRY_POLICY, "persistent") &&
            plan->spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds < 1000000 &&
            !push(warnings, json_object_new_string("rapid-persistent-retry")))
            goto allocation;
        if (!add(root, "schema_version", json_object_new_int(1)) ||
            !add(root, "command", json_object_new_string("plan")) ||
            !add(root, "ok", json_object_new_boolean(true)) ||
            !add(root, "exit_code", json_object_new_int(0)) ||
            !add(root, "live_validation", json_object_new_string("not_performed")) ||
            !add(root, "collision_check", json_object_new_string("not_performed")) ||
            !add(root, "replayable", json_object_new_boolean(false)) ||
            !add(root, "profile", json_object_new_string("generic")) ||
            !add(root, "profile_matching", json_object_new_string("disabled")) ||
            !add(root, "unit_redacted", json_object_new_boolean(redacted)) ||
            !add(root, "unit", json_object_new_string_len(unit.data, (int)unit.length)))
            goto allocation;
        bool added = add(root, "fields", fields);
        fields = NULL;
        if (!added)
            goto allocation;
        added = add(root, "environment", environment);
        environment = NULL;
        if (!added)
            goto allocation;
        added = add(root, "warnings", warnings);
        warnings = NULL;
        if (!added)
            goto allocation;
        const char *encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
        if (encoded == NULL)
            goto allocation;
        for (size_t i = 0; encoded[i] != '\0'; ++i)
        {
            if ((unsigned char)encoded[i] == 0xc2 && (unsigned char)encoded[i + 1] >= 0x80 &&
                (unsigned char)encoded[i + 1] <= 0x9f)
            {
                static const char hex[] = "0123456789abcdef";
                unsigned char c = (unsigned char)encoded[++i];
                char escape[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                if (!tired_buffer_append(&text, escape, sizeof(escape), error))
                    goto fail;
            }
            else if (!tired_buffer_append(&text, encoded + i, 1, error))
                goto fail;
        }
        if (!tired_buffer_append(&text, "\n", 1, error))
            goto fail;
    }
    else
    {
        const char *heading =
            redacted ? "# Redacted non-installable view; live validation not performed\n"
                     : "# Offline proposal; live validation and collision checks not performed\n";
        if (!tired_buffer_append(&text, heading, strlen(heading), error))
            goto fail;
        if (!unit_only &&
            !tired_buffer_append(
                &text, "# Profile: Generic; application requirements unknown\n",
                sizeof("# Profile: Generic; application requirements unknown\n") - 1, error))
            goto fail;
        if (sensitive &&
            !tired_buffer_append(
                &text, "# Warning: sensitive-command-data; actual unit retains command secrets\n",
                sizeof("# Warning: sensitive-command-data; actual unit retains command secrets\n") -
                    1,
                error))
            goto fail;
        /* Token/scalar encoders prevent raw terminal control sequences. */
        if (!tired_buffer_append(&text, unit.data, unit.length, error))
            goto fail;
    }
    if (!tired_buffer_take(&text, output, error))
        goto fail;
    tired_spec_destroy(&display);
    tired_text_destroy(&unit);
    json_object_put(root);
    json_object_put(fields);
    json_object_put(environment);
    json_object_put(warnings);
    return true;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot construct plan output.", 0);
fail:
    tired_spec_destroy(&display);
    tired_text_destroy(&unit);
    tired_buffer_destroy(&text);
    json_object_put(root);
    json_object_put(fields);
    json_object_put(environment);
    json_object_put(warnings);
    return false;
}
