#include "tired/model.h"
#include "tired/capture.h"
#include "tired/name.h"

#include <assert.h>
#include <string.h>

#define FIELD(id, name, section, directive, kind, choices, fallback, min, max, path)               \
    [TIRED_FIELD_##id] = {TIRED_FIELD_##id, name,     section, directive, TIRED_FIELD_##kind,      \
                          choices,          fallback, min,     max,       path}
static const TiredField fields[TIRED_FIELD_COUNT] = {
    FIELD(NAME, "name", NULL, NULL, TEXT, NULL, NULL, 0, 200, false),
    FIELD(DESCRIPTION, "description", "Unit", "Description", TEXT, NULL, NULL, 0, 4096, false),
    FIELD(SCOPE, "scope", NULL, NULL, CHOICE, "system|user", "system", 0, 0, false),
    FIELD(RUN_AS, "run_as", "Service", "User", TEXT, NULL, NULL, 1, 256, false),
    FIELD(GROUP, "group", "Service", "Group", TEXT, NULL, NULL, 1, 256, false),
    FIELD(EXECUTABLE, "executable", "Service", "ExecStart", TEXT, NULL, NULL, 1, TIRED_INPUT_LIMIT,
          true),
    FIELD(WORKING_DIRECTORY, "working_directory", "Service", "WorkingDirectory", TEXT, NULL, NULL,
          1, TIRED_INPUT_LIMIT, true),
    FIELD(TYPE, "type", "Service", "Type", CHOICE, "exec|simple|notify|forking|oneshot", "exec", 0,
          0, false),
    FIELD(PID_FILE, "pid_file", "Service", "PIDFile", TEXT, NULL, NULL, 1, TIRED_INPUT_LIMIT, true),
    FIELD(REMAIN_AFTER_EXIT, "remain_after_exit", "Service", "RemainAfterExit", BOOL, NULL, NULL, 0,
          0, false),
    FIELD(RESTART, "restart", "Service", "Restart", CHOICE,
          "no|on-failure|always|on-abnormal|on-success|on-abort|on-watchdog", "on-failure", 0, 0,
          false),
    FIELD(RESTART_SEC, "restart_sec", "Service", "RestartSec", DURATION, NULL, "5s", 0, 0, false),
    FIELD(RETRY_POLICY, "retry_policy", NULL, NULL, CHOICE, "persistent|limited", "persistent", 0,
          0, false),
    FIELD(START_LIMIT_INTERVAL, "start_limit_interval", "Unit", "StartLimitIntervalSec", DURATION,
          NULL, "0", 0, 0, false),
    FIELD(START_LIMIT_BURST, "start_limit_burst", "Unit", "StartLimitBurst", INTEGER, NULL, NULL, 1,
          UINT32_MAX, false),
    FIELD(TIMEOUT_START, "timeout_start", "Service", "TimeoutStartSec", DURATION, NULL, NULL, 0, 0,
          false),
    FIELD(TIMEOUT_STOP, "timeout_stop", "Service", "TimeoutStopSec", DURATION, NULL, "30s", 0, 0,
          false),
    FIELD(KILL_MODE, "kill_mode", "Service", "KillMode", CHOICE, "control-group|mixed|process|none",
          "control-group", 0, 0, false),
    FIELD(STANDARD_INPUT, "standard_input", "Service", "StandardInput", CHOICE, "null", "null", 0,
          0, false),
    FIELD(STANDARD_OUTPUT, "standard_output", "Service", "StandardOutput", CHOICE, "journal|null",
          "journal", 0, 0, false),
    FIELD(STANDARD_ERROR, "standard_error", "Service", "StandardError", CHOICE, "journal|null",
          "journal", 0, 0, false),
    FIELD(SYSLOG_IDENTIFIER, "syslog_identifier", "Service", "SyslogIdentifier", TEXT, NULL, NULL,
          1, 256, false),
    FIELD(NICE, "nice", "Service", "Nice", INTEGER, NULL, NULL, -20, 19, false),
    FIELD(NO_NEW_PRIVILEGES, "no_new_privileges", "Service", "NoNewPrivileges", BOOL, NULL, NULL, 0,
          0, false),
    FIELD(PRIVATE_TMP, "private_tmp", "Service", "PrivateTmp", BOOL, NULL, NULL, 0, 0, false),
    FIELD(PROTECT_SYSTEM, "protect_system", "Service", "ProtectSystem", CHOICE,
          "false|true|full|strict", NULL, 0, 0, false),
    FIELD(PROTECT_HOME, "protect_home", "Service", "ProtectHome", CHOICE,
          "false|true|read-only|tmpfs", NULL, 0, 0, false),
    FIELD(NETWORK, "network", NULL, NULL, CHOICE, "none|network|online", "none", 0, 0, false),
    FIELD(START, "start", NULL, NULL, BOOL, NULL, "true", 0, 0, false),
    FIELD(ENABLE, "enable", NULL, NULL, BOOL, NULL, "true", 0, 0, false),
    FIELD(ENABLE_LINGER, "enable_linger", NULL, NULL, BOOL, NULL, "false", 0, 0, false),
};
#undef FIELD

const TiredField *tired_field_get(TiredFieldId id)
{
    return (unsigned)id < TIRED_FIELD_COUNT ? &fields[id] : NULL;
}

const TiredField *tired_field_find(const char *name, size_t length)
{
    assert(name != NULL || length == 0);
    for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (strlen(fields[i].name) == length && memcmp(fields[i].name, name, length) == 0)
            return &fields[i];
    return NULL;
}

bool tired_parse_duration(const char *text, size_t length, uint64_t *microseconds,
                          TiredError *error)
{
    assert(text != NULL || length == 0);
    assert(microseconds != NULL);
    size_t end = 0;
    while (end < length && ((text[end] >= '0' && text[end] <= '9') || text[end] == '.'))
        ++end;
    uint64_t scale = 1000000;
    size_t suffix = length - end;
    if (suffix == 2 && memcmp(text + end, "us", 2) == 0)
        scale = 1;
    else if (suffix == 2 && memcmp(text + end, "ms", 2) == 0)
        scale = 1000;
    else if (suffix == 1 && text[end] == 's')
        scale = 1000000;
    else if (suffix == 3 && memcmp(text + end, "min", 3) == 0)
        scale = 60000000;
    else if (suffix == 1 && text[end] == 'h')
        scale = 3600000000ULL;
    else if (suffix == 1 && text[end] == 'd')
        scale = 86400000000ULL;
    else if (suffix != 0)
        goto invalid;
    size_t dot = 0;
    while (dot < end && text[dot] != '.')
        ++dot;
    uint64_t whole;
    if (!tired_parse_u64(text, dot, 0, UINT64_MAX / scale, &whole, error))
        return false;
    uint64_t result = whole * scale;
    if (dot != end)
    {
        size_t digits = end - dot - 1;
        if (digits == 0 || digits > 6)
            goto invalid;
        uint64_t fraction;
        if (!tired_parse_u64(text + dot + 1, digits, 0, 999999, &fraction, error))
            return false;
        uint64_t divisor = 1;
        for (size_t i = 0; i < digits; ++i)
            divisor *= 10;
        uint64_t product = fraction * scale; /* <= 999999 * 86400000000 < UINT64_MAX */
        if (product % divisor != 0 || product / divisor > UINT64_MAX - result)
            goto invalid;
        result += product / divisor;
    }
    *microseconds = result;
    tired_error_clear(error);
    return true;
invalid:
    return tired_error_set(error, TIRED_INVALID, "duration",
                           "Expected a nonnegative duration with exact microsecond precision and "
                           "suffix us, ms, s, min, h, or d.",
                           0);
}

static bool choice_index(const char *choices, const char *text, size_t length, size_t *index)
{
    size_t i = 0;
    while (choices != NULL)
    {
        const char *next = strchr(choices, '|');
        size_t count = next == NULL ? strlen(choices) : (size_t)(next - choices);
        if (count == length && memcmp(choices, text, length) == 0)
        {
            *index = i;
            return true;
        }
        choices = next == NULL ? NULL : next + 1;
        ++i;
    }
    return false;
}

static bool has_value(const TiredFieldValue *value)
{
    return value->origin != TIRED_ORIGIN_UNSET && value->origin != TIRED_ORIGIN_INHERITED;
}

bool tired_spec_set(TiredServiceSpec *spec, TiredFieldId id, const char *text, size_t length,
                    TiredFieldOrigin origin, bool replace, TiredError *error)
{
    assert(spec != NULL && (text != NULL || length == 0));
    const TiredField *field = tired_field_get(id);
    if (field == NULL || origin < TIRED_ORIGIN_DEFAULT || origin > TIRED_ORIGIN_CAPTURE)
        return tired_error_set(error, TIRED_INVALID, "field",
                               "Unknown field or invalid assignment origin.", 0);
    if (!replace && spec->fields[id].origin == TIRED_ORIGIN_USER && origin == TIRED_ORIGIN_USER)
        return tired_error_set(error, TIRED_INVALID, "duplicate-field",
                               "A scalar field was assigned more than once.", 0);
    if (length > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "input-limit",
                               "Field exceeds the input limit.", 0);
    if (!tired_validate_text(text, length, field->absolute_path, error))
        return false;
    TiredFieldValue next = {.origin = origin};
    switch (field->kind)
    {
    case TIRED_FIELD_TEXT:
        if (length < (uint64_t)field->minimum ||
            length > (uint64_t)field->maximum + (id == TIRED_FIELD_NAME ? 8U : 0U) ||
            (field->absolute_path && (length == 0 || text[0] != '/')))
            return tired_error_set(error, TIRED_INVALID, "field-text",
                                   "Field length or absolute-path requirement is invalid.", 0);
        if (id == TIRED_FIELD_NAME)
        {
            if (!tired_name_explicit(text, length, &next.value.text, error))
                return false;
        }
        else if (!tired_text_set(&next.value.text, text, length, (size_t)field->maximum, error))
            return false;
        break;
    case TIRED_FIELD_CHOICE:
        if (!choice_index(field->choices, text, length, &next.value.choice))
            return tired_error_set(error, TIRED_INVALID, "field-choice",
                                   "Unsupported field choice.", 0);
        break;
    case TIRED_FIELD_BOOL:
        if (length == 4 && memcmp(text, "true", 4) == 0)
            next.value.boolean = true;
        else if (length == 5 && memcmp(text, "false", 5) == 0)
            next.value.boolean = false;
        else
            return tired_error_set(error, TIRED_INVALID, "field-boolean", "Expected true or false.",
                                   0);
        break;
    case TIRED_FIELD_INTEGER:
        if (!tired_parse_i64(text, length, field->minimum, field->maximum, &next.value.integer,
                             error))
            return false;
        break;
    case TIRED_FIELD_DURATION:
        if (!tired_parse_duration(text, length, &next.value.microseconds, error))
            return false;
        break;
    }
    if (field->kind == TIRED_FIELD_TEXT && has_value(&spec->fields[id]))
        tired_text_destroy(&spec->fields[id].value.text);
    spec->fields[id] = next;
    tired_error_clear(error);
    return true;
}

void tired_spec_destroy(TiredServiceSpec *spec)
{
    if (spec == NULL)
        return;
    for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (fields[i].kind == TIRED_FIELD_TEXT && has_value(&spec->fields[i]))
            tired_text_destroy(&spec->fields[i].value.text);
    *spec = (TiredServiceSpec){0};
}

bool tired_spec_defaults(TiredServiceSpec *spec, TiredError *error)
{
    assert(spec != NULL);
    TiredServiceSpec result = {0};
    for (size_t i = 0; i < TIRED_FIELD_COUNT; ++i)
    {
        const TiredField *field = &fields[i];
        if (field->default_value == NULL)
            result.fields[i].origin = TIRED_ORIGIN_INHERITED;
        else if (!tired_spec_set(&result, field->id, field->default_value,
                                 strlen(field->default_value), TIRED_ORIGIN_DEFAULT, false, error))
        {
            tired_spec_destroy(&result);
            return false;
        }
    }
    tired_spec_destroy(spec);
    *spec = result;
    tired_error_clear(error);
    return true;
}

bool tired_spec_choice_is(const TiredServiceSpec *spec, TiredFieldId id, const char *choice)
{
    assert(spec != NULL && choice != NULL);
    const TiredField *field = tired_field_get(id);
    size_t index;
    return field != NULL && field->kind == TIRED_FIELD_CHOICE && has_value(&spec->fields[id]) &&
           choice_index(field->choices, choice, strlen(choice), &index) &&
           index == spec->fields[id].value.choice;
}

bool tired_spec_validate_scalars(const TiredServiceSpec *spec, TiredError *error)
{
    assert(spec != NULL);
    if (tired_spec_choice_is(spec, TIRED_FIELD_TYPE, "oneshot") &&
        (tired_spec_choice_is(spec, TIRED_FIELD_RESTART, "always") ||
         tired_spec_choice_is(spec, TIRED_FIELD_RESTART, "on-success")))
        return tired_error_set(error, TIRED_INVALID, "oneshot-restart",
                               "Oneshot services cannot restart always or on success.", 0);
    if (tired_spec_choice_is(spec, TIRED_FIELD_TYPE, "forking") &&
        !has_value(&spec->fields[TIRED_FIELD_PID_FILE]))
        return tired_error_set(error, TIRED_INVALID, "forking-pid-file",
                               "Forking services require an explicit PID file.", 0);
    if (tired_spec_choice_is(spec, TIRED_FIELD_RETRY_POLICY, "persistent"))
    {
        if (has_value(&spec->fields[TIRED_FIELD_RESTART_SEC]) &&
            spec->fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 0)
            return tired_error_set(error, TIRED_INVALID, "restart-delay",
                                   "Persistent retries require a nonzero delay.", 0);
        if (has_value(&spec->fields[TIRED_FIELD_START_LIMIT_INTERVAL]) &&
            spec->fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds != 0)
            return tired_error_set(error, TIRED_INVALID, "persistent-rate-limit",
                                   "Persistent retries require a zero start-limit interval.", 0);
    }
    if (tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "user") &&
        has_value(&spec->fields[TIRED_FIELD_NETWORK]) &&
        !tired_spec_choice_is(spec, TIRED_FIELD_NETWORK, "none"))
        return tired_error_set(error, TIRED_INVALID, "user-network-target",
                               "System network targets cannot be added to a user service.", 0);
    if (tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "system") &&
        has_value(&spec->fields[TIRED_FIELD_ENABLE_LINGER]) &&
        spec->fields[TIRED_FIELD_ENABLE_LINGER].value.boolean)
        return tired_error_set(error, TIRED_INVALID, "linger-scope",
                               "Lingering applies only to user services.", 0);
    tired_error_clear(error);
    return true;
}
