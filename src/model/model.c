#include "tired/model.h"
#include "tired/capture.h"
#include "tired/name.h"
#include "tired/process_value.h"

#include <assert.h>
#include <string.h>

#define FIELD(id, name, section, directive, kind, choices, fallback, min, max, path)               \
    [TIRED_FIELD_##id] = {TIRED_FIELD_##id, name,     section, directive, TIRED_FIELD_##kind,      \
                          choices,          fallback, min,     max,       path}
static const TiredField fields[TIRED_FIELD_COUNT] = {
    FIELD(KILL_SIGNAL, "kill_signal", "Service", "KillSignal", SIGNAL, NULL, "SIGTERM", 0, 0,
          false),
    FIELD(SUCCESS_EXIT_STATUS, "success_exit_status", "Service", "SuccessExitStatus", LIST, NULL,
          NULL, 1, 32, false),
    FIELD(RESTART_PREVENT_EXIT_STATUS, "restart_prevent_exit_status", "Service",
          "RestartPreventExitStatus", LIST, NULL, NULL, 1, 32, false),
    FIELD(CAPABILITY_BOUNDING_SET, "capability_bounding_set", "Service", "CapabilityBoundingSet",
          LIST, NULL, NULL, 1, 64, false),
    FIELD(AMBIENT_CAPABILITIES, "ambient_capabilities", "Service", "AmbientCapabilities", LIST,
          NULL, NULL, 1, 64, false),
    FIELD(WANTED_BY, "wanted_by", "Install", "WantedBy", CHOICE, "multi-user.target|default.target",
          NULL, 0, 0, false),
    FIELD(NOFILE_SOFT, "nofile.soft", "Service", "LimitNOFILE", LIMIT, NULL, NULL, 0, 0, false),
    FIELD(NOFILE_HARD, "nofile.hard", "Service", "LimitNOFILE", LIMIT, NULL, NULL, 0, 0, false),
    FIELD(MEMORY_MAX, "memory_max", "Service", "MemoryMax", LIMIT, NULL, NULL, 0, 0, false),
    FIELD(TASKS_MAX, "tasks_max", "Service", "TasksMax", LIMIT, NULL, NULL, 1, 0, false),
    FIELD(CPU_QUOTA, "cpu_quota", "Service", "CPUQuota", QUOTA, NULL, NULL, 0, 0, false),
    FIELD(UMASK, "umask", "Service", "UMask", MODE, NULL, NULL, 0, 0777, false),
    FIELD(RUNTIME_DIRECTORY_MODE, "runtime_directory_mode", "Service", "RuntimeDirectoryMode", MODE,
          NULL, NULL, 0, 07777, false),
    FIELD(STATE_DIRECTORY_MODE, "state_directory_mode", "Service", "StateDirectoryMode", MODE, NULL,
          NULL, 0, 07777, false),
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
    FIELD(ARGV, "argv", "Service", "ExecStart", LIST, NULL, NULL, 0, TIRED_INPUT_LIMIT, false),
    FIELD(SUPPLEMENTARY_GROUPS, "supplementary_groups", "Service", "SupplementaryGroups", LIST,
          NULL, NULL, 1, 256, false),
    FIELD(ENVIRONMENT_FILES, "environment_files", "Service", "EnvironmentFile", LIST, NULL, NULL, 1,
          TIRED_INPUT_LIMIT, true),
    FIELD(AFTER, "after", "Unit", "After", LIST, NULL, NULL, 1, 255, false),
    FIELD(WANTS, "wants", "Unit", "Wants", LIST, NULL, NULL, 1, 255, false),
    FIELD(REQUIRES, "requires", "Unit", "Requires", LIST, NULL, NULL, 1, 255, false),
    FIELD(REQUIRES_MOUNTS_FOR, "requires_mounts_for", "Unit", "RequiresMountsFor", LIST, NULL, NULL,
          1, TIRED_INPUT_LIMIT, true),
    FIELD(READ_WRITE_PATHS, "read_write_paths", "Service", "ReadWritePaths", LIST, NULL, NULL, 1,
          TIRED_INPUT_LIMIT, true),
    FIELD(RUNTIME_DIRECTORY, "runtime_directory", "Service", "RuntimeDirectory", LIST, NULL, NULL,
          1, 4096, false),
    FIELD(STATE_DIRECTORY, "state_directory", "Service", "StateDirectory", LIST, NULL, NULL, 1,
          4096, false),
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
    case TIRED_FIELD_SIGNAL:
        if (!tired_parse_signal(text, length, &next.value.signal_number, error))
            return false;
        break;
    case TIRED_FIELD_LIMIT:
        if (!tired_parse_limit(text, length, id == TIRED_FIELD_MEMORY_MAX, &next.value.limit,
                               error))
            return false;
        if (!next.value.limit.infinity && next.value.limit.value < (uint64_t)field->minimum)
            return tired_error_set(error, TIRED_INVALID, "resource-minimum",
                                   "Resource limit is below the allowed minimum.", 0);
        break;
    case TIRED_FIELD_MODE:
        if (!tired_parse_mode(text, length, (uint32_t)field->maximum, &next.value.mode, error))
            return false;
        break;
    case TIRED_FIELD_QUOTA:
        if (!tired_parse_quota(text, length, &next.value.quota, error))
            return false;
        break;
    case TIRED_FIELD_LIST:
        return tired_error_set(error, TIRED_INVALID, "field-collection",
                               "Collection fields require the list assignment API.", 0);
    case TIRED_FIELD_TEXT:
        if (length < (uint64_t)field->minimum ||
            length > (uint64_t)field->maximum + (id == TIRED_FIELD_NAME ? 8U : 0U) ||
            (field->absolute_path && (length == 0 || text[0] != '/')))
            return tired_error_set(error, TIRED_INVALID, "field-text",
                                   "Field length or absolute-path requirement is invalid.", 0);
        if (id == TIRED_FIELD_NAME && origin == TIRED_ORIGIN_CAPTURE)
        {
            if (!tired_name_validate_base(text, length, error) ||
                !tired_text_set(&next.value.text, text, length, TIRED_EXPLICIT_NAME_LIMIT, error))
                return false;
        }
        else if (id == TIRED_FIELD_NAME)
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
    {
        if (fields[i].kind == TIRED_FIELD_TEXT && has_value(&spec->fields[i]))
            tired_text_destroy(&spec->fields[i].value.text);
        if (fields[i].kind == TIRED_FIELD_LIST && has_value(&spec->fields[i]))
            tired_text_list_destroy(&spec->fields[i].value.list);
    }
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
    if (tired_spec_choice_is(spec, TIRED_FIELD_RETRY_POLICY, "limited") &&
        (!has_value(&spec->fields[TIRED_FIELD_START_LIMIT_INTERVAL]) ||
         spec->fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds == 0 ||
         !has_value(&spec->fields[TIRED_FIELD_START_LIMIT_BURST]) ||
         spec->fields[TIRED_FIELD_START_LIMIT_BURST].value.integer <= 0))
        return tired_error_set(
            error, TIRED_INVALID, "limited-rate-limit",
            "Limited retries require a positive interval and burst; resolve defaults first.", 0);
    if (has_value(&spec->fields[TIRED_FIELD_WANTED_BY]) &&
        ((tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "user") &&
          !tired_spec_choice_is(spec, TIRED_FIELD_WANTED_BY, "default.target")) ||
         (tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "system") &&
          !tired_spec_choice_is(spec, TIRED_FIELD_WANTED_BY, "multi-user.target"))))
        return tired_error_set(error, TIRED_INVALID, "enablement-scope",
                               "Enablement target does not match the selected scope.", 0);
    if (has_value(&spec->fields[TIRED_FIELD_CAPABILITY_BOUNDING_SET]) &&
        has_value(&spec->fields[TIRED_FIELD_AMBIENT_CAPABILITIES]))
    {
        const TiredTextList *ambient = &spec->fields[TIRED_FIELD_AMBIENT_CAPABILITIES].value.list;
        const TiredTextList *bounding =
            &spec->fields[TIRED_FIELD_CAPABILITY_BOUNDING_SET].value.list;
        for (size_t i = 0; i < ambient->count; ++i)
        {
            bool found = false;
            for (size_t j = 0; j < bounding->count; ++j)
                if (strcmp(ambient->items[i].data, bounding->items[j].data) == 0)
                    found = true;
            if (!found)
                return tired_error_set(
                    error, TIRED_INVALID, "capability-bounds",
                    "Ambient capability is absent from the explicit bounding set.", 0);
        }
    }
    bool soft = has_value(&spec->fields[TIRED_FIELD_NOFILE_SOFT]);
    bool hard = has_value(&spec->fields[TIRED_FIELD_NOFILE_HARD]);
    if (soft != hard)
        return tired_error_set(error, TIRED_INVALID, "nofile-pair",
                               "Specify both soft and hard NOFILE limits.", 0);
    if (soft && !tired_limit_le(spec->fields[TIRED_FIELD_NOFILE_SOFT].value.limit,
                                spec->fields[TIRED_FIELD_NOFILE_HARD].value.limit))
        return tired_error_set(error, TIRED_INVALID, "nofile-order",
                               "Soft NOFILE limit exceeds the hard limit.", 0);
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

static bool ascii_identifier(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

static bool safe_relative_directory(const char *text, size_t length)
{
    if (length == 0 || text[0] == '/' || text[length - 1] == '/')
        return false;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i)
    {
        if (i == length || text[i] == '/')
        {
            size_t size = i - start;
            if (size == 0 || (size == 1 && text[start] == '.') ||
                (size == 2 && text[start] == '.' && text[start + 1] == '.'))
                return false;
            start = i + 1;
        }
        else if (!ascii_identifier((unsigned char)text[i]) && text[i] != '.')
            return false;
    }
    return true;
}

static bool safe_unit_reference(const char *text, size_t length)
{
    const char *dot = NULL;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)text[i];
        if (!ascii_identifier(c) && c != '.' && c != '@' && c != ':')
            return false;
        if (c == '.')
        {
            if (i == 0 || (i != 0 && text[i - 1] == '.'))
                return false;
            dot = text + i;
        }
    }
    if (dot == NULL || dot == text || dot == text + length - 1)
        return false;
    const char *types[] = {"service", "target", "socket", "mount", "automount", "swap",
                           "timer",   "path",   "slice",  "scope", "device"};
    size_t suffix_length = length - (size_t)(dot - text) - 1;
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
        if (strlen(types[i]) == suffix_length && memcmp(dot + 1, types[i], suffix_length) == 0)
            return true;
    return false;
}

bool tired_spec_append(TiredServiceSpec *spec, TiredFieldId id, const char *text, size_t length,
                       TiredFieldOrigin origin, TiredError *error)
{
    assert(spec != NULL && (text != NULL || length == 0));
    const TiredField *field = tired_field_get(id);
    if (field == NULL || field->kind != TIRED_FIELD_LIST || origin < TIRED_ORIGIN_DEFAULT ||
        origin > TIRED_ORIGIN_CAPTURE)
        return tired_error_set(error, TIRED_INVALID, "field-collection",
                               "Expected a collection field and assignment origin.", 0);
    if (length < (uint64_t)field->minimum || length > (uint64_t)field->maximum ||
        (field->absolute_path && (length == 0 || text[0] != '/')))
        return tired_error_set(error, TIRED_INVALID, "collection-item",
                               "Collection item length or path is invalid.", 0);
    if (!tired_validate_text(text, length, id != TIRED_FIELD_ARGV, error))
        return false;
    bool valid = true;
    if (id == TIRED_FIELD_CAPABILITY_BOUNDING_SET || id == TIRED_FIELD_AMBIENT_CAPABILITIES)
    {
        unsigned number;
        if (!tired_parse_capability(text, length, &number, error))
            return false;
    }
    if (id == TIRED_FIELD_SUCCESS_EXIT_STATUS || id == TIRED_FIELD_RESTART_PREVENT_EXIT_STATUS)
    {
        uint64_t status;
        int signal_number;
        if (length != 0 && text[0] >= '0' && text[0] <= '9')
        {
            if (!tired_parse_u64(text, length, 0, 255, &status, error))
                return false;
        }
        else if (!tired_parse_signal(text, length, &signal_number, error))
            return false;
    }
    if (id == TIRED_FIELD_SUPPLEMENTARY_GROUPS)
        for (size_t i = 0; i < length; ++i)
            if (!ascii_identifier((unsigned char)text[i]) && text[i] != '.')
                valid = false;
    if (id == TIRED_FIELD_AFTER || id == TIRED_FIELD_WANTS || id == TIRED_FIELD_REQUIRES)
        valid = safe_unit_reference(text, length);
    if (id == TIRED_FIELD_RUNTIME_DIRECTORY || id == TIRED_FIELD_STATE_DIRECTORY)
        valid = safe_relative_directory(text, length);
    if (!valid)
        return tired_error_set(error, TIRED_INVALID, "collection-item",
                               "Unsupported collection item syntax.", 0);
    TiredFieldValue *destination = &spec->fields[id];
    /* Callers resolve provenance before merging lists; do not silently erase it. */
    if (has_value(destination) && destination->origin != origin)
        return tired_error_set(error, TIRED_INVALID, "collection-origin",
                               "Resolve collection precedence before appending another origin.", 0);
    size_t count_limit = id == TIRED_FIELD_ARGV ? TIRED_ARGUMENT_LIMIT : 1024;
    if (!tired_text_list_append(&destination->value.list, text, length, count_limit,
                                TIRED_INPUT_LIMIT, error))
        return false;
    destination->origin = origin;
    return true;
}

bool tired_spec_clear_list(TiredServiceSpec *spec, TiredFieldId id, TiredFieldOrigin origin,
                           TiredError *error)
{
    assert(spec != NULL);
    const TiredField *field = tired_field_get(id);
    if (field == NULL || field->kind != TIRED_FIELD_LIST || origin < TIRED_ORIGIN_DEFAULT ||
        origin > TIRED_ORIGIN_CAPTURE)
        return tired_error_set(error, TIRED_INVALID, "field-collection",
                               "Expected a collection field and assignment origin.", 0);
    tired_text_list_destroy(&spec->fields[id].value.list);
    spec->fields[id].origin = origin;
    tired_error_clear(error);
    return true;
}

bool tired_spec_resolve_retry(TiredServiceSpec *spec, TiredError *error)
{
    assert(spec != NULL);
    bool limited = tired_spec_choice_is(spec, TIRED_FIELD_RETRY_POLICY, "limited");
    if (!limited && !tired_spec_choice_is(spec, TIRED_FIELD_RETRY_POLICY, "persistent"))
        return tired_error_set(error, TIRED_INVALID, "retry-policy",
                               "Retry policy must be specified before resolving defaults.", 0);
    TiredFieldValue interval = spec->fields[TIRED_FIELD_START_LIMIT_INTERVAL];
    TiredFieldValue burst = spec->fields[TIRED_FIELD_START_LIMIT_BURST];
    if (interval.origin <= TIRED_ORIGIN_DEFAULT)
    {
        interval.origin = TIRED_ORIGIN_DEFAULT;
        interval.value.microseconds = limited ? 300000000 : 0;
    }
    if (burst.origin <= TIRED_ORIGIN_DEFAULT)
    {
        burst =
            (TiredFieldValue){.origin = limited ? TIRED_ORIGIN_DEFAULT : TIRED_ORIGIN_INHERITED};
        if (limited)
            burst.value.integer = 10;
    }
    if ((limited && interval.value.microseconds == 0) ||
        (!limited && interval.value.microseconds != 0))
        return tired_error_set(error, TIRED_INVALID, "retry-interval",
                               "Start-limit interval conflicts with the selected retry policy.", 0);
    if (!limited && has_value(&spec->fields[TIRED_FIELD_RESTART_SEC]) &&
        spec->fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 0)
        return tired_error_set(error, TIRED_INVALID, "restart-delay",
                               "Persistent retries require a nonzero delay.", 0);
    spec->fields[TIRED_FIELD_START_LIMIT_INTERVAL] = interval;
    spec->fields[TIRED_FIELD_START_LIMIT_BURST] = burst;
    tired_error_clear(error);
    return true;
}

bool tired_spec_resolve_scope(TiredServiceSpec *spec, TiredError *error)
{
    assert(spec != NULL);
    bool user = tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "user");
    if (!user && !tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "system"))
        return tired_error_set(error, TIRED_INVALID, "scope",
                               "Select a scope before resolving its defaults.", 0);
    const char *target = user ? "default.target" : "multi-user.target";
    if (spec->fields[TIRED_FIELD_WANTED_BY].origin <= TIRED_ORIGIN_DEFAULT)
        return tired_spec_set(spec, TIRED_FIELD_WANTED_BY, target, strlen(target),
                              TIRED_ORIGIN_DEFAULT, true, error);
    if (!tired_spec_choice_is(spec, TIRED_FIELD_WANTED_BY, target))
        return tired_error_set(error, TIRED_INVALID, "enablement-scope",
                               "Enablement target does not match the selected scope.", 0);
    tired_error_clear(error);
    return true;
}
