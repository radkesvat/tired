#include "tired/encode.h"
#include "tired/settings.h"
#include <assert.h>
#include <inttypes.h>
#include <json-c/json.h>
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
static struct json_object *duration(uint64_t usec)
{
    char text[32];
    (void)snprintf(text, sizeof(text), "%" PRIu64 "us", usec);
    return json_object_new_string(text);
}
bool tired_settings_output(const TiredSettings *settings, bool json, TiredText *output,
                           TiredError *error)
{
    assert(settings != NULL && output != NULL && settings->color <= 2);
    static const char *names[] = {"color",        "ascii",           "tui",
                                  "retry_policy", "restart_sec",     "history_revisions",
                                  "log_tail",     "observation_sec", "profile_directories"};
    static const char *origins[] = {"default", "administrator", "user", "cli"};
    static const char *colors[] = {"auto", "always", "never"};
    struct json_object *values[TIRED_SETTING_COUNT] = {
        json_object_new_string(colors[settings->color]),
        json_object_new_boolean(settings->ascii),
        json_object_new_boolean(settings->tui),
        json_object_new_string(settings->limited_retries ? "limited" : "persistent"),
        duration(settings->restart_usec),
        json_object_new_uint64(settings->history_revisions),
        json_object_new_uint64(settings->log_tail),
        duration(settings->observation_usec),
        json_object_new_array()};
    struct json_object *result = json_object_new_object(), *fields = json_object_new_object();
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = false;
    if (result == NULL || fields == NULL)
        goto allocation;
    for (size_t i = 0; i < TIRED_SETTING_COUNT; ++i)
        if (values[i] == NULL)
            goto allocation;
    for (size_t i = 0; i < settings->profile_directories.count; ++i)
    {
        struct json_object *path =
            json_object_new_string(settings->profile_directories.items[i].data);
        if (path == NULL)
            goto allocation;
        if (json_object_array_add(values[TIRED_SETTING_PROFILE_DIRECTORIES], path) != 0)
        {
            json_object_put(path);
            goto allocation;
        }
    }
    for (size_t i = 0; i < TIRED_SETTING_COUNT; ++i)
    {
        assert(settings->origins[i] <= TIRED_SETTINGS_CLI);
        const char *origin = origins[settings->origins[i]];
        if (!json)
        {
            const char *value = json_object_to_json_string_ext(values[i], JSON_C_TO_STRING_PLAIN);
            if (value == NULL)
                goto allocation;
            if (!tired_buffer_append(&buffer, names[i], strlen(names[i]), error) ||
                !tired_buffer_append(&buffer, " = ", 3, error) ||
                !tired_buffer_append(&buffer, value, strlen(value), error) ||
                !tired_buffer_append(&buffer, " [", 2, error) ||
                !tired_buffer_append(&buffer, origin, strlen(origin), error) ||
                !tired_buffer_append(&buffer, "]\n", 2, error))
                goto done;
        }
        else
        {
            struct json_object *field = json_object_new_object();
            if (field == NULL)
                goto allocation;
            bool inserted = add(field, "value", values[i]);
            values[i] = NULL;
            if (!inserted || !add(field, "origin", json_object_new_string(origin)))
            {
                json_object_put(field);
                goto allocation;
            }
            if (!add(fields, names[i], field))
                goto allocation;
        }
    }
    if (json)
    {
        bool inserted = add(result, "settings", fields);
        fields = NULL;
        if (!inserted || !add(result, "schema_version", json_object_new_int(1)) ||
            !add(result, "command", json_object_new_string("config show")) ||
            !add(result, "ok", json_object_new_boolean(true)) ||
            !add(result, "exit_code", json_object_new_int(0)))
            goto allocation;
        const char *encoded = json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY);
        if (encoded == NULL)
            goto allocation;
        if (!tired_buffer_append(&buffer, encoded, strlen(encoded), error) ||
            !tired_buffer_append(&buffer, "\n", 1, error))
            goto done;
    }
    ok = tired_buffer_take(&buffer, output, error);
    if (ok)
        tired_error_clear(error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate settings output.", 0);
done:
    for (size_t i = 0; i < TIRED_SETTING_COUNT; ++i)
        json_object_put(values[i]);
    json_object_put(fields);
    json_object_put(result);
    tired_buffer_destroy(&buffer);
    return ok;
}
