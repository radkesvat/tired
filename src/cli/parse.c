#include "tired/capture.h"
#include "tired/cli.h"
#include "tired/journal_stream.h"
#include "tired/risk.h"
#include <assert.h>
#include <string.h>

static const char *commands[TIRED_COMMAND_COUNT] = {
    "dashboard", "create", "plan",    "list",     "status",  "show", "logs",
    "start",     "stop",   "restart", "enable",   "disable", "edit", "rename",
    "remove",    "doctor", "recover", "profiles", "config"};
const char *tired_command_name(TiredCommand command)
{
    return (unsigned)command < TIRED_COMMAND_COUNT ? commands[command] : NULL;
}

void tired_request_destroy(TiredRequest *r)
{
    if (r == NULL)
        return;
    tired_spec_destroy(&r->overrides);
    tired_environment_destroy(&r->environment);
    tired_text_list_destroy(&r->arguments);
    tired_text_list_destroy(&r->replacement);
    tired_text_list_destroy(&r->pass_environment);
    tired_text_list_destroy(&r->import_files);
    tired_text_list_destroy(&r->environment_files);
    tired_text_list_destroy(&r->credentials);
    tired_text_list_destroy(&r->allowed_risks);
    tired_text_destroy(&r->profile);
    tired_text_destroy(&r->working_directory);
    tired_text_destroy(&r->output);
    tired_text_destroy(&r->color);
    tired_text_destroy(&r->apply_mode);
    tired_text_destroy(&r->resolution);
    tired_text_destroy(&r->transaction);
    tired_text_destroy(&r->active_filter);
    tired_text_destroy(&r->enabled_filter);
    tired_text_destroy(&r->search);
    *r = (TiredRequest){0};
}

static bool list_add(TiredTextList *list, const char *value, TiredError *error)
{
    return tired_text_list_append(list, value, strlen(value), TIRED_ARGUMENT_LIMIT,
                                  TIRED_INPUT_LIMIT, error);
}
static bool text_set(TiredText *text, const char *value, TiredError *error)
{
    if (text->data != NULL)
        return tired_error_set(error, TIRED_INVALID, "duplicate-option",
                               "A scalar option was supplied more than once.", 0);
    return tired_text_set(text, value, strlen(value), TIRED_INPUT_LIMIT, error);
}
static bool mark(bool *flag, TiredError *error)
{
    if (*flag)
        return tired_error_set(error, TIRED_INVALID, "duplicate-option",
                               "A flag was supplied more than once.", 0);
    *flag = true;
    return true;
}
static bool field_set(TiredRequest *r, TiredFieldId id, const char *value, TiredError *error)
{
    const TiredField *field = tired_field_get(id);
    if (field == NULL)
        return tired_error_set(error, TIRED_INVALID, "unknown-field", "Unknown typed field.", 0);
    if (r->overrides.fields[id].inherit)
        return tired_error_set(error, TIRED_INVALID, "duplicate-field",
                               "A field cannot be both assigned and explicitly inherited.", 0);
    if (id == TIRED_FIELD_ARGV || id == TIRED_FIELD_EXECUTABLE)
        return tired_error_set(error, TIRED_INVALID, "command-field",
                               "Set the command through its explicit argv boundary.", 0);
    if (field->kind == TIRED_FIELD_LIST)
    {
        bool appended =
            tired_spec_append(&r->overrides, id, value, strlen(value), TIRED_ORIGIN_USER, error);
        if (appended)
            r->seen[id] = true;
        return appended;
    }
    if (r->seen[id])
        return tired_error_set(error, TIRED_INVALID, "duplicate-field",
                               "A field was assigned more than once.", 0);
    bool ok;
    if (id == TIRED_FIELD_WORKING_DIRECTORY)
        ok = value[0] != '\0' && tired_validate_text(value, strlen(value), true, error) &&
             text_set(&r->working_directory, value, error);
    else
        ok = tired_spec_set(&r->overrides, id, value, strlen(value), TIRED_ORIGIN_USER, false,
                            error);
    if (ok)
        r->seen[id] = true;
    else if (id == TIRED_FIELD_WORKING_DIRECTORY && value[0] == '\0')
        tired_error_set(error, TIRED_INVALID, "working-directory",
                        "Working directory cannot be empty.", 0);
    return ok;
}

static const struct
{
    const char *option;
    TiredFieldId field;
    const char *fixed;
} field_options[] = {{"--system", TIRED_FIELD_SCOPE, "system"},
                     {"--user", TIRED_FIELD_SCOPE, "user"},
                     {"--name", TIRED_FIELD_NAME, NULL},
                     {"--description", TIRED_FIELD_DESCRIPTION, NULL},
                     {"--working-directory", TIRED_FIELD_WORKING_DIRECTORY, NULL},
                     {"--run-as", TIRED_FIELD_RUN_AS, NULL},
                     {"--group", TIRED_FIELD_GROUP, NULL},
                     {"--type", TIRED_FIELD_TYPE, NULL},
                     {"--start", TIRED_FIELD_START, "true"},
                     {"--no-start", TIRED_FIELD_START, "false"},
                     {"--enable", TIRED_FIELD_ENABLE, "true"},
                     {"--no-enable", TIRED_FIELD_ENABLE, "false"},
                     {"--restart", TIRED_FIELD_RESTART, NULL},
                     {"--restart-sec", TIRED_FIELD_RESTART_SEC, NULL},
                     {"--retry-policy", TIRED_FIELD_RETRY_POLICY, NULL},
                     {"--start-limit-interval", TIRED_FIELD_START_LIMIT_INTERVAL, NULL},
                     {"--start-limit-burst", TIRED_FIELD_START_LIMIT_BURST, NULL},
                     {"--network", TIRED_FIELD_NETWORK, NULL},
                     {"--enable-linger", TIRED_FIELD_ENABLE_LINGER, "true"}};

static const char *const value_options[] = {
    "--set",           "--nofile",          "--profile",    "--env",           "--pass-env",
    "--env-file",      "--import-env-file", "--credential", "--allow-risk",    "--output",
    "--color",         "--hardening",       "--unset",      "--sensitive-arg", "--active-state",
    "--enabled-state", "--search",          "--lines",      "--since",         "--boot",
    "--apply-mode",    "--resolution",      "--transaction"};

static bool takes_value(const char *option)
{
    for (size_t i = 0; i < sizeof(field_options) / sizeof(field_options[0]); ++i)
        if (strcmp(option, field_options[i].option) == 0)
            return field_options[i].fixed == NULL;
    for (size_t i = 0; i < sizeof(value_options) / sizeof(value_options[0]); ++i)
        if (strcmp(option, value_options[i]) == 0)
            return true;
    return false;
}

/* Select presentation independently of semantic parsing, so an earlier invalid
 * value cannot turn a later frontend --json into a human-only failure. Values
 * and workload tokens retain the exact same boundaries as the actual parser. */
static bool json_presentation(int argc, const char *const *argv)
{
    bool selected = false, explain = false;
    TiredCommand command = TIRED_COMMAND_DASHBOARD;
    size_t operands = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0)
            break;
        if (arg[0] != '-' || arg[1] == '\0')
        {
            if (!selected)
            {
                for (unsigned j = 1; j < TIRED_COMMAND_COUNT; ++j)
                    if (strcmp(arg, commands[j]) == 0)
                    {
                        command = (TiredCommand)j;
                        selected = true;
                        break;
                    }
                if (!selected)
                    break;
                continue;
            }
            if (command == TIRED_COMMAND_CREATE || command == TIRED_COMMAND_PLAN || explain)
                break;
            if (command == TIRED_COMMAND_PROFILES && operands == 0 && strcmp(arg, "explain") == 0)
                explain = true;
            ++operands;
            continue;
        }
        if (strcmp(arg, "--json") == 0)
            return true;
        size_t length = strnlen(arg, 64);
        const char *equal = memchr(arg, '=', length);
        if (equal != NULL)
            length = (size_t)(equal - arg);
        if (length < 64)
        {
            char option[64];
            memcpy(option, arg, length);
            option[length] = '\0';
            if (equal == NULL && takes_value(option) && i + 1 < argc)
                ++i;
        }
    }
    return false;
}

static const char *option_value(int argc, const char *const *argv, int *position,
                                const char *inline_value, TiredError *error)
{
    if (inline_value != NULL)
        return inline_value;
    if (*position + 1 >= argc)
    {
        tired_error_set(error, TIRED_INVALID, "option-value", "Option requires a value.", 0);
        return NULL;
    }
    return argv[++*position];
}

bool tired_cli_parse_format(int argc, const char *const *argv, TiredRequest *request,
                            bool *json_requested, TiredError *error)
{
    assert(argc >= 0 && (argv != NULL || argc == 0) && request != NULL);
    TiredRequest parsed = {0};
    bool presentation_json = json_requested != NULL && json_presentation(argc, argv);
    if (json_requested != NULL)
        *json_requested = presentation_json;
    bool selected = false, creation_options = false;
    size_t input_bytes = 0;
    if (argc > (int)TIRED_ARGUMENT_LIMIT + 1)
        return tired_error_set(error, TIRED_INVALID, "argument-limit",
                               "Frontend argument count exceeds its limit.", 0);
    for (int i = 1; i < argc; ++i)
    {
        size_t length = strnlen(argv[i], TIRED_INPUT_LIMIT);
        if (length >= TIRED_INPUT_LIMIT - input_bytes)
            return tired_error_set(error, TIRED_INVALID, "argument-limit",
                                   "Frontend arguments exceed the byte limit.", 0);
        input_bytes += length + 1;
    }
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0)
        {
            parsed.explicit_boundary = true;
            if (!selected)
            {
                parsed.command = TIRED_COMMAND_CREATE;
                selected = true;
            }
            bool replacement = parsed.command == TIRED_COMMAND_EDIT;
            if (replacement && parsed.arguments.count != 1)
                goto inappropriate;
            for (++i; i < argc; ++i)
                if (!list_add(replacement ? &parsed.replacement : &parsed.arguments, argv[i],
                              error))
                    goto fail;
            if (replacement && parsed.replacement.count == 0)
                goto inappropriate;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0')
        {
            if (!selected)
            {
                for (unsigned j = 1; j < TIRED_COMMAND_COUNT; ++j)
                    if (strcmp(arg, commands[j]) == 0)
                    {
                        parsed.command = (TiredCommand)j;
                        selected = true;
                        break;
                    }
                if (selected)
                    continue;
                parsed.command = TIRED_COMMAND_CREATE;
                selected = true;
            }
            if (parsed.command == TIRED_COMMAND_PROFILES && parsed.arguments.count == 0 &&
                !parsed.profile_explain && strcmp(arg, "explain") == 0)
            {
                parsed.profile_explain = true;
                continue;
            }
            if (parsed.profile_explain)
            {
                tired_error_set(error, TIRED_INVALID, "explain-boundary",
                                "Use profiles explain [options] -- COMMAND [ARG...].", 0);
                goto fail;
            }
            if (parsed.command == TIRED_COMMAND_CREATE || parsed.command == TIRED_COMMAND_PLAN)
            {
                for (; i < argc; ++i)
                    if (!list_add(&parsed.arguments, argv[i], error))
                        goto fail;
                break;
            }
            if (!list_add(&parsed.arguments, arg, error))
                goto fail;
            continue;
        }
        const char *equal = strchr(arg, '=');
        size_t option_length = equal == NULL ? strlen(arg) : (size_t)(equal - arg);
        char option[64];
        if (option_length >= sizeof(option))
            goto unknown;
        memcpy(option, arg, option_length);
        option[option_length] = '\0';
        const char *value = equal == NULL ? NULL : equal + 1;
        bool handled = false;
        for (size_t j = 0; j < sizeof(field_options) / sizeof(field_options[0]); ++j)
            if (strcmp(option, field_options[j].option) == 0)
            {
                if (field_options[j].fixed != NULL)
                {
                    if (value != NULL)
                        goto flag_value;
                    value = field_options[j].fixed;
                }
                else
                    value = option_value(argc, argv, &i, value, error);
                if (value == NULL || !field_set(&parsed, field_options[j].field, value, error))
                    goto fail;
                if (field_options[j].field != TIRED_FIELD_SCOPE)
                    creation_options = true;
                handled = true;
                break;
            }
        if (handled)
            continue;
        struct
        {
            const char *name;
            bool *flag;
        } flags[] = {{"--help", &parsed.help},
                     {"--version", &parsed.version},
                     {"--json", &parsed.json},
                     {"--no-tui", &parsed.no_tui},
                     {"--yes", &parsed.yes},
                     {"--quiet", &parsed.quiet},
                     {"--verbose", &parsed.verbose},
                     {"--offline", &parsed.offline},
                     {"--unit", &parsed.unit},
                     {"--dry-run", &parsed.dry_run},
                     {"--check-active", &parsed.check_active},
                     {"--effective", &parsed.effective},
                     {"--now", &parsed.now},
                     {"--keep-history", &parsed.keep_history},
                     {"--refresh-profile", &parsed.refresh_profile},
                     {"--restore-managed", &parsed.restore_drift},
                     {"--build-info", &parsed.build_info},
                     {"--follow", &parsed.logs.follow},
                     {"--include-sensitive", &parsed.include_sensitive}};
        for (size_t j = 0; j < sizeof(flags) / sizeof(flags[0]); ++j)
            if (strcmp(option, flags[j].name) == 0)
            {
                if (value != NULL)
                    goto flag_value;
                if (!mark(flags[j].flag, error))
                    goto fail;
                handled = true;
                break;
            }
        if (handled)
            continue;
        handled = takes_value(option);
        if (!handled)
            goto unknown;
        value = option_value(argc, argv, &i, value, error);
        if (value == NULL)
            goto fail;
        if (strcmp(option, "--apply-mode") == 0)
        {
            if ((strcmp(value, "restart") != 0 && strcmp(value, "defer") != 0) ||
                !text_set(&parsed.apply_mode, value, error))
                goto inappropriate;
        }
        else if (strcmp(option, "--resolution") == 0)
        {
            if ((strcmp(value, "finish") != 0 && strcmp(value, "rollback") != 0) ||
                !text_set(&parsed.resolution, value, error))
                goto inappropriate;
        }
        else if (strcmp(option, "--transaction") == 0)
        {
            if (!text_set(&parsed.transaction, value, error))
                goto fail;
        }
        else if (strcmp(option, "--lines") == 0)
        {
            uint64_t lines;
            if (!mark(&parsed.logs.lines_set, error) ||
                !tired_parse_u64(value, strlen(value), 0, TIRED_JOURNAL_TAIL_LIMIT, &lines, error))
                goto fail;
            parsed.logs.lines = (size_t)lines;
        }
        else if (strcmp(option, "--since") == 0)
        {
            if (!mark(&parsed.logs.since_set, error) ||
                !tired_log_since_parse(value, &parsed.logs.since_usec, error))
                goto fail;
        }
        else if (strcmp(option, "--boot") == 0)
        {
            if (!mark(&parsed.logs.boot_set, error) ||
                !tired_log_boot_parse(value, &parsed.logs, error))
                goto fail;
        }
        else if (strcmp(option, "--color") == 0)
        {
            if (strcmp(value, "auto") != 0 && strcmp(value, "always") != 0 &&
                strcmp(value, "never") != 0)
            {
                tired_error_set(error, TIRED_INVALID, "color",
                                "Color must be auto, always, or never.", 0);
                goto fail;
            }
            if (!text_set(&parsed.color, value, error))
                goto fail;
        }
        else if (strcmp(option, "--output") == 0)
        {
            if (!text_set(&parsed.output, value, error))
                goto fail;
        }
        else if (strcmp(option, "--profile") == 0)
        {
            if (!text_set(&parsed.profile, value, error))
                goto fail;
        }
        else if (strcmp(option, "--hardening") == 0)
        {
            creation_options = true;
            if (strcmp(value, "baseline") != 0)
            {
                tired_error_set(error, TIRED_INVALID, "hardening-preset",
                                "The supported opt-in hardening preset is baseline.", 0);
                goto fail;
            }
            if (!field_set(&parsed, TIRED_FIELD_NO_NEW_PRIVILEGES, "true", error) ||
                !field_set(&parsed, TIRED_FIELD_PRIVATE_TMP, "true", error) ||
                !field_set(&parsed, TIRED_FIELD_PROTECT_SYSTEM, "full", error))
                goto fail;
        }
        else if (strcmp(option, "--active-state") == 0 || strcmp(option, "--enabled-state") == 0 ||
                 strcmp(option, "--search") == 0)
        {
            size_t length = strlen(value);
            if (length == 0 || length > 255 || !tired_validate_text(value, length, true, error))
            {
                tired_error_set(
                    error, TIRED_INVALID, "list-filter",
                    "List filters require nonempty text of at most 255 bytes without controls.", 0);
                goto fail;
            }
            TiredText *filter = strcmp(option, "--active-state") == 0    ? &parsed.active_filter
                                : strcmp(option, "--enabled-state") == 0 ? &parsed.enabled_filter
                                                                         : &parsed.search;
            if (!text_set(filter, value, error))
                goto fail;
        }
        else if (strcmp(option, "--allow-risk") == 0)
        {
            TiredRiskId id;
            if (!tired_risk_find(value, strlen(value), &id))
            {
                tired_error_set(error, TIRED_INVALID, "unknown-risk",
                                "Risk acknowledgment names an unsupported code.", 0);
                goto fail;
            }
            if (!list_add(&parsed.allowed_risks, value, error))
                goto fail;
        }
        else
        {
            creation_options = true;
            if (strcmp(option, "--sensitive-arg") == 0)
            {
                uint64_t index;
                if (!tired_parse_u64(value, strlen(value), 1, TIRED_ARGUMENT_LIMIT - 1, &index,
                                     error))
                    goto fail;
                parsed.sensitive_arguments[index] = true;
                continue;
            }
            if (strcmp(option, "--unset") == 0)
            {
                const TiredField *field = tired_field_find(value, strlen(value));
                if (field == NULL)
                {
                    tired_error_set(error, TIRED_INVALID, "unknown-field",
                                    "Expected a known optional field to inherit.", 0);
                    goto fail;
                }
                if (parsed.overrides.fields[field->id].origin == TIRED_ORIGIN_USER ||
                    (field->id == TIRED_FIELD_ENVIRONMENT_FILES &&
                     parsed.environment_files.count != 0))
                {
                    tired_error_set(error, TIRED_INVALID, "duplicate-field",
                                    "A field cannot be both assigned and explicitly inherited.", 0);
                    goto fail;
                }
                if (!tired_spec_inherit(&parsed.overrides, field->id, error))
                    goto fail;
                parsed.seen[field->id] = true;
            }
            else if (strcmp(option, "--set") == 0)
            {
                const char *separator = strchr(value, '=');
                const TiredField *field =
                    separator == NULL ? NULL : tired_field_find(value, (size_t)(separator - value));
                if (field == NULL)
                {
                    tired_error_set(error, TIRED_INVALID, "unknown-field",
                                    "Expected a known typed FIELD=VALUE.", 0);
                    goto fail;
                }
                if (!field_set(&parsed, field->id, separator + 1, error))
                    goto fail;
            }
            else if (strcmp(option, "--nofile") == 0)
            {
                const char *separator = strchr(value, ':');
                if (separator == NULL || separator == value || (size_t)(separator - value) >= 32)
                {
                    tired_error_set(error, TIRED_INVALID, "nofile", "Expected NOFILE SOFT:HARD.",
                                    0);
                    goto fail;
                }
                char soft[32];
                memcpy(soft, value, (size_t)(separator - value));
                soft[separator - value] = '\0';
                if (!field_set(&parsed, TIRED_FIELD_NOFILE_SOFT, soft, error) ||
                    !field_set(&parsed, TIRED_FIELD_NOFILE_HARD, separator + 1, error))
                    goto fail;
            }
            else if (strcmp(option, "--env") == 0)
            {
                if (!tired_environment_set(&parsed.environment, value, strlen(value),
                                           TIRED_ENV_EXPLICIT, false, error))
                    goto fail;
            }
            else
            {
                if (strcmp(option, "--env-file") == 0 &&
                    parsed.overrides.fields[TIRED_FIELD_ENVIRONMENT_FILES].inherit)
                {
                    tired_error_set(error, TIRED_INVALID, "duplicate-field",
                                    "Environment files cannot be both assigned and inherited.", 0);
                    goto fail;
                }
                TiredTextList *destination =
                    strcmp(option, "--pass-env") == 0          ? &parsed.pass_environment
                    : strcmp(option, "--env-file") == 0        ? &parsed.environment_files
                    : strcmp(option, "--import-env-file") == 0 ? &parsed.import_files
                                                               : &parsed.credentials;
                if (!list_add(destination, value, error))
                    goto fail;
            }
        }
    }
    if (parsed.dry_run)
    {
        if (parsed.command != TIRED_COMMAND_CREATE)
            goto inappropriate;
        parsed.command = TIRED_COMMAND_PLAN;
    }
    if (parsed.offline && parsed.command != TIRED_COMMAND_PLAN)
        goto inappropriate;
    if ((parsed.now && parsed.command != TIRED_COMMAND_ENABLE &&
         parsed.command != TIRED_COMMAND_DISABLE) ||
        (parsed.keep_history && parsed.command != TIRED_COMMAND_REMOVE) ||
        ((parsed.apply_mode.data != NULL || parsed.refresh_profile || parsed.restore_drift) &&
         parsed.command != TIRED_COMMAND_EDIT) ||
        ((parsed.resolution.data != NULL || parsed.transaction.data != NULL) &&
         (parsed.command != TIRED_COMMAND_RECOVER || parsed.resolution.data == NULL ||
          parsed.transaction.data == NULL)))
        goto inappropriate;
    if (tired_field_has_value(&parsed.overrides.fields[TIRED_FIELD_ENABLE_LINGER]) &&
        parsed.overrides.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean &&
        (!tired_spec_choice_is(&parsed.overrides, TIRED_FIELD_SCOPE, "user") || parsed.unit ||
         (parsed.command != TIRED_COMMAND_CREATE && parsed.command != TIRED_COMMAND_PLAN &&
          parsed.command != TIRED_COMMAND_EDIT)))
        goto inappropriate;
    if (parsed.unit && parsed.command != TIRED_COMMAND_PLAN && parsed.command != TIRED_COMMAND_SHOW)
        goto inappropriate;
    if (parsed.output.data != NULL && parsed.command != TIRED_COMMAND_PLAN &&
        parsed.command != TIRED_COMMAND_SHOW)
        goto inappropriate;
    if (creation_options && parsed.command != TIRED_COMMAND_CREATE &&
        parsed.command != TIRED_COMMAND_PLAN && parsed.command != TIRED_COMMAND_EDIT &&
        !parsed.profile_explain)
        goto inappropriate;
    if (parsed.profile.data != NULL && parsed.command != TIRED_COMMAND_CREATE &&
        parsed.command != TIRED_COMMAND_PLAN && parsed.command != TIRED_COMMAND_EDIT &&
        parsed.command != TIRED_COMMAND_LIST && !parsed.profile_explain)
        goto inappropriate;
    if ((parsed.active_filter.data != NULL || parsed.enabled_filter.data != NULL ||
         parsed.search.data != NULL) &&
        parsed.command != TIRED_COMMAND_LIST)
        goto inappropriate;
    if (parsed.command == TIRED_COMMAND_LIST && parsed.profile.data != NULL &&
        (parsed.profile.length == 0 || parsed.profile.length > 255 ||
         !tired_validate_text(parsed.profile.data, parsed.profile.length, true, error)))
        goto inappropriate;
    if (parsed.quiet && parsed.verbose)
        goto inappropriate;
    if (parsed.check_active && parsed.command != TIRED_COMMAND_STATUS)
        goto inappropriate;
    if (parsed.effective && parsed.command != TIRED_COMMAND_SHOW)
        goto inappropriate;
    if ((parsed.logs.follow || parsed.logs.lines_set || parsed.logs.since_set ||
         parsed.logs.boot_set) &&
        parsed.command != TIRED_COMMAND_LOGS)
        goto inappropriate;
    if (parsed.unit && parsed.json)
        goto inappropriate;
    if (parsed.include_sensitive && parsed.output.data == NULL)
        goto inappropriate;
    if (!parsed.help && !parsed.version && !parsed.build_info)
    {
        size_t count = parsed.arguments.count;
        size_t sensitive_count =
            parsed.command == TIRED_COMMAND_EDIT
                ? (parsed.replacement.count == 0 ? TIRED_ARGUMENT_LIMIT : parsed.replacement.count)
                : count;
        for (size_t i = 1; i < TIRED_ARGUMENT_LIMIT; ++i)
            if (parsed.sensitive_arguments[i] &&
                (i >= sensitive_count ||
                 (parsed.command != TIRED_COMMAND_CREATE && parsed.command != TIRED_COMMAND_PLAN &&
                  parsed.command != TIRED_COMMAND_EDIT && !parsed.profile_explain)))
            {
                tired_error_set(error, TIRED_INVALID, "sensitive-argument-index",
                                "Sensitive argument index must identify an existing workload "
                                "argument after the executable.",
                                0);
                goto fail;
            }
        bool valid;
        switch (parsed.command)
        {
        case TIRED_COMMAND_CREATE:
        case TIRED_COMMAND_PLAN:
            valid = count > 0;
            break;
        case TIRED_COMMAND_DASHBOARD:
        case TIRED_COMMAND_LIST:
        case TIRED_COMMAND_RECOVER:
            valid = count == 0;
            break;
        case TIRED_COMMAND_DOCTOR:
            valid = count <= 1;
            break;
        case TIRED_COMMAND_RENAME:
            valid = count == 2;
            break;
        case TIRED_COMMAND_PROFILES:
            valid = parsed.profile_explain ? parsed.explicit_boundary && count > 0 : count >= 1;
            break;
        case TIRED_COMMAND_CONFIG:
            valid = count >= 1;
            break;
        default:
            valid = count == 1;
            break;
        }
        if (!valid)
        {
            tired_error_set(error, TIRED_INVALID, "command-arguments",
                            "Wrong number of command arguments.", 0);
            goto fail;
        }
    }
    if (json_requested != NULL)
        *json_requested = parsed.json;
    tired_request_destroy(request);
    *request = parsed;
    tired_error_clear(error);
    return true;
unknown:
    tired_error_set(error, TIRED_INVALID, "unknown-option",
                    "Unknown frontend option before the command boundary.", 0);
    goto fail;
flag_value:
    tired_error_set(error, TIRED_INVALID, "flag-value",
                    "Boolean flags do not accept an attached value.", 0);
    goto fail;
inappropriate:
    tired_error_set(error, TIRED_INVALID, "option-context",
                    "Option is incompatible with this command or another option.", 0);
fail:
    if (json_requested != NULL)
        *json_requested = presentation_json || parsed.json;
    tired_request_destroy(&parsed);
    return false;
}

bool tired_cli_parse(int argc, const char *const *argv, TiredRequest *request, TiredError *error)
{
    return tired_cli_parse_format(argc, argv, request, NULL, error);
}
