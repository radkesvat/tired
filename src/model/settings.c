#include "tired/settings.h"
#include "tired/capture.h"
#include "tired/json.h"
#include "tired/model.h"
#include <assert.h>
#include <string.h>

static const char *names[TIRED_SETTING_COUNT] = {
    "color",        "ascii",           "tui",
    "retry_policy", "restart_sec",     "history_revisions",
    "log_tail",     "observation_sec", "profile_directories"};
void tired_settings_destroy(TiredSettings *settings)
{
    if (settings == NULL)
        return;
    tired_text_list_destroy(&settings->profile_directories);
    *settings = (TiredSettings){0};
}
void tired_settings_defaults(TiredSettings *settings)
{
    assert(settings != NULL);
    tired_settings_destroy(settings);
    settings->tui = true;
    settings->restart_usec = 5000000;
    settings->history_revisions = 32;
    settings->log_tail = 200;
    settings->observation_usec = 3000000;
}
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "settings-schema",
                           "Settings contain an unknown, invalid, or unsupported field.", 0);
}
static bool validate(const TiredSettings *settings, TiredError *error)
{
    if (!settings->limited_retries && settings->restart_usec == 0)
        return tired_error_set(error, TIRED_INVALID, "settings-retry",
                               "Persistent retry defaults require a nonzero delay.", 0);
    return true;
}

bool tired_settings_parse(const char *data, size_t length, TiredSettings *settings,
                          TiredError *error)
{
    assert(settings != NULL);
    struct json_object *document = NULL, *schema = NULL;
    TiredSettings parsed = {0};
    tired_settings_defaults(&parsed);
    if (!tired_json_parse(data, length, TIRED_INPUT_LIMIT, &document, error))
        goto fail;
    uint64_t version;
    if (!json_object_is_type(document, json_type_object) ||
        !json_object_object_get_ex(document, "schema_version", &schema) ||
        !tired_json_u64(schema, 1, 1, &version, error))
    {
        invalid(error);
        goto fail;
    }
    json_object_object_foreach(document, key, value)
    {
        if (strcmp(key, "schema_version") == 0)
            continue;
        size_t id = TIRED_SETTING_COUNT;
        for (size_t i = 0; i < TIRED_SETTING_COUNT; ++i)
            if (strcmp(key, names[i]) == 0)
                id = i;
        if (id == TIRED_SETTING_COUNT)
        {
            invalid(error);
            goto fail;
        }
        parsed.supplied[id] = true;
        const char *text =
            json_object_is_type(value, json_type_string) ? json_object_get_string(value) : NULL;
        switch ((TiredSettingId)id)
        {
        case TIRED_SETTING_COLOR:
            if (text == NULL)
            {
                invalid(error);
                goto fail;
            }
            if (strcmp(text, "auto") == 0)
                parsed.color = 0;
            else if (strcmp(text, "always") == 0)
                parsed.color = 1;
            else if (strcmp(text, "never") == 0)
                parsed.color = 2;
            else
            {
                invalid(error);
                goto fail;
            }
            break;
        case TIRED_SETTING_ASCII:
        case TIRED_SETTING_TUI:
            if (!json_object_is_type(value, json_type_boolean))
            {
                invalid(error);
                goto fail;
            }
            if (id == TIRED_SETTING_ASCII)
                parsed.ascii = json_object_get_boolean(value);
            else
                parsed.tui = json_object_get_boolean(value);
            break;
        case TIRED_SETTING_RETRY:
            if (text == NULL || (strcmp(text, "persistent") != 0 && strcmp(text, "limited") != 0))
            {
                invalid(error);
                goto fail;
            }
            parsed.limited_retries = strcmp(text, "limited") == 0;
            break;
        case TIRED_SETTING_RESTART_DELAY:
        case TIRED_SETTING_OBSERVATION:
        {
            uint64_t duration;
            if (text == NULL || !tired_parse_duration(text, strlen(text), &duration, error))
            {
                invalid(error);
                goto fail;
            }
            if (id == TIRED_SETTING_RESTART_DELAY)
            {
                if (duration > UINT64_C(86400000000))
                {
                    invalid(error);
                    goto fail;
                }
                parsed.restart_usec = duration;
            }
            else
            {
                if (duration < 100000 || duration > 300000000)
                {
                    invalid(error);
                    goto fail;
                }
                parsed.observation_usec = duration;
            }
            break;
        }
        case TIRED_SETTING_HISTORY:
            if (!tired_json_u64(value, 1, 1000, &parsed.history_revisions, error))
                goto fail;
            break;
        case TIRED_SETTING_LOG_TAIL:
            if (!tired_json_u64(value, 1, 10000, &parsed.log_tail, error))
                goto fail;
            break;
        case TIRED_SETTING_PROFILE_DIRECTORIES:
            if (!json_object_is_type(value, json_type_array) ||
                json_object_array_length(value) > 16)
            {
                invalid(error);
                goto fail;
            }
            for (size_t i = 0; i < json_object_array_length(value); ++i)
            {
                struct json_object *entry = json_object_array_get_idx(value, i);
                if (!json_object_is_type(entry, json_type_string))
                {
                    invalid(error);
                    goto fail;
                }
                const char *path = json_object_get_string(entry);
                size_t size = (size_t)json_object_get_string_len(entry);
                if (size == 0 || path[0] != '/' || !tired_validate_text(path, size, true, error))
                {
                    invalid(error);
                    goto fail;
                }
                if (!tired_text_list_append(&parsed.profile_directories, path, size, 16, 65536,
                                            error))
                    goto fail;
            }
            break;
        case TIRED_SETTING_COUNT:
            break;
        }
    }
    /* A partial layer may rely on the preceding layer's retry policy or delay. */
    if (parsed.supplied[TIRED_SETTING_RETRY] && parsed.supplied[TIRED_SETTING_RESTART_DELAY] &&
        !validate(&parsed, error))
        goto fail;
    json_object_put(document);
    tired_settings_destroy(settings);
    *settings = parsed;
    tired_error_clear(error);
    return true;
fail:
    json_object_put(document);
    tired_settings_destroy(&parsed);
    return false;
}

bool tired_settings_merge(TiredSettings *settings, const TiredSettings *input,
                          TiredSettingsOrigin origin, TiredError *error)
{
    assert(settings != NULL && input != NULL);
    if (origin != TIRED_SETTINGS_ADMIN && origin != TIRED_SETTINGS_USER)
        return invalid(error);
    if (origin == TIRED_SETTINGS_USER && input->supplied[TIRED_SETTING_PROFILE_DIRECTORIES])
        return tired_error_set(error, TIRED_INVALID, "settings-trust",
                               "User settings cannot redefine administrative profile locations.",
                               0);
    TiredSettings result = *settings;
    result.profile_directories = (TiredTextList){0};
    const TiredTextList *directories = input->supplied[TIRED_SETTING_PROFILE_DIRECTORIES]
                                           ? &input->profile_directories
                                           : &settings->profile_directories;
    for (size_t i = 0; i < directories->count; ++i)
        if (!tired_text_list_append(&result.profile_directories, directories->items[i].data,
                                    directories->items[i].length, 16, 65536, error))
            goto fail;
    for (size_t i = 0; i < TIRED_SETTING_COUNT; ++i)
    {
        if (!input->supplied[i])
            continue;
        result.supplied[i] = true;
        result.origins[i] = origin;
        switch ((TiredSettingId)i)
        {
        case TIRED_SETTING_COLOR:
            result.color = input->color;
            break;
        case TIRED_SETTING_ASCII:
            result.ascii = input->ascii;
            break;
        case TIRED_SETTING_TUI:
            result.tui = input->tui;
            break;
        case TIRED_SETTING_RETRY:
            result.limited_retries = input->limited_retries;
            break;
        case TIRED_SETTING_RESTART_DELAY:
            result.restart_usec = input->restart_usec;
            break;
        case TIRED_SETTING_HISTORY:
            result.history_revisions = input->history_revisions;
            break;
        case TIRED_SETTING_LOG_TAIL:
            result.log_tail = input->log_tail;
            break;
        case TIRED_SETTING_OBSERVATION:
            result.observation_usec = input->observation_usec;
            break;
        case TIRED_SETTING_PROFILE_DIRECTORIES:
        case TIRED_SETTING_COUNT:
            break;
        }
    }
    if (!validate(&result, error))
        goto fail;
    tired_settings_destroy(settings);
    *settings = result;
    tired_error_clear(error);
    return true;
fail:
    tired_settings_destroy(&result);
    return false;
}
