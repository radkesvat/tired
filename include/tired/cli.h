#ifndef TIRED_CLI_H
#define TIRED_CLI_H
#include "tired/environment.h"
#include "tired/model.h"

typedef enum
{
    TIRED_COMMAND_DASHBOARD,
    TIRED_COMMAND_CREATE,
    TIRED_COMMAND_PLAN,
    TIRED_COMMAND_LIST,
    TIRED_COMMAND_STATUS,
    TIRED_COMMAND_SHOW,
    TIRED_COMMAND_LOGS,
    TIRED_COMMAND_START,
    TIRED_COMMAND_STOP,
    TIRED_COMMAND_RESTART,
    TIRED_COMMAND_ENABLE,
    TIRED_COMMAND_DISABLE,
    TIRED_COMMAND_EDIT,
    TIRED_COMMAND_RENAME,
    TIRED_COMMAND_REMOVE,
    TIRED_COMMAND_DOCTOR,
    TIRED_COMMAND_RECOVER,
    TIRED_COMMAND_PROFILES,
    TIRED_COMMAND_CONFIG,
    TIRED_COMMAND_COUNT
} TiredCommand;

typedef struct
{
    TiredCommand command;
    TiredServiceSpec overrides;
    bool seen[TIRED_FIELD_COUNT];
    TiredTextList arguments; /* Workload argv or management operands. */
    TiredEnvironment environment;
    TiredTextList pass_environment, import_files, environment_files, credentials, allowed_risks;
    TiredText profile, working_directory, output, color;
    bool help, version, json, no_tui, yes, quiet, verbose, offline, unit, dry_run,
        include_sensitive;
    bool explicit_boundary;
    bool profile_explain;
    bool sensitive_arguments[TIRED_ARGUMENT_LIMIT]; /* Workload argv indices; index 0 excluded. */
} TiredRequest;
/* Parse argv including the frontend executable. No filesystem, environment,
 * manager, or workload access. Output is atomic and owns copies. */
bool tired_cli_parse(int argc, const char *const *argv, TiredRequest *request, TiredError *error);
/* Reports a recognized --json flag even when later parsing fails. */
bool tired_cli_parse_format(int argc, const char *const *argv, TiredRequest *request,
                            bool *json_requested, TiredError *error);
const char *tired_command_name(TiredCommand command);
void tired_request_destroy(TiredRequest *request);
#endif
