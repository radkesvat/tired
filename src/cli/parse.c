#include "tired/capture.h"
#include "tired/cli.h"
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
    tired_text_list_destroy(&r->pass_environment);
    tired_text_list_destroy(&r->import_files);
    tired_text_list_destroy(&r->environment_files);
    tired_text_list_destroy(&r->credentials);
    tired_text_list_destroy(&r->allowed_risks);
    tired_text_destroy(&r->profile);
    tired_text_destroy(&r->working_directory);
    tired_text_destroy(&r->output);
    tired_text_destroy(&r->color);
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
        return tired_spec_append(&r->overrides, id, value, strlen(value), TIRED_ORIGIN_USER, error);
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
    if (json_requested != NULL)
        *json_requested = false;
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
            for (++i; i < argc; ++i)
                if (!list_add(&parsed.arguments, argv[i], error))
                    goto fail;
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
        const char *taking[] = {"--set",        "--nofile",   "--profile",         "--env",
                                "--pass-env",   "--env-file", "--import-env-file", "--credential",
                                "--allow-risk", "--output",   "--color",           "--unset"};
        for (size_t j = 0; j < sizeof(taking) / sizeof(taking[0]); ++j)
            if (strcmp(option, taking[j]) == 0)
                handled = true;
        if (!handled)
            goto unknown;
        value = option_value(argc, argv, &i, value, error);
        if (value == NULL)
            goto fail;
        if (strcmp(option, "--color") == 0)
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
        else if (strcmp(option, "--allow-risk") == 0)
        {
            if (!list_add(&parsed.allowed_risks, value, error))
                goto fail;
        }
        else
        {
            creation_options = true;
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
            else if (strcmp(option, "--profile") == 0)
            {
                if (!text_set(&parsed.profile, value, error))
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
    if (parsed.unit && parsed.command != TIRED_COMMAND_PLAN && parsed.command != TIRED_COMMAND_SHOW)
        goto inappropriate;
    if (parsed.output.data != NULL && parsed.command != TIRED_COMMAND_PLAN &&
        parsed.command != TIRED_COMMAND_SHOW)
        goto inappropriate;
    if (creation_options && parsed.command != TIRED_COMMAND_CREATE &&
        parsed.command != TIRED_COMMAND_PLAN && parsed.command != TIRED_COMMAND_EDIT &&
        !parsed.profile_explain)
        goto inappropriate;
    if (parsed.quiet && parsed.verbose)
        goto inappropriate;
    if (parsed.unit && parsed.json)
        goto inappropriate;
    if (parsed.include_sensitive && parsed.output.data == NULL)
        goto inappropriate;
    if (!parsed.help && !parsed.version)
    {
        size_t count = parsed.arguments.count;
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
        *json_requested = parsed.json;
    tired_request_destroy(&parsed);
    return false;
}

bool tired_cli_parse(int argc, const char *const *argv, TiredRequest *request, TiredError *error)
{
    return tired_cli_parse_format(argc, argv, request, NULL, error);
}
