#include "tired/cli.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define PARSE(...)                                                                                 \
    parse(&request, &error, (const char *const[]){__VA_ARGS__},                                    \
          sizeof((const char *const[]){__VA_ARGS__}) / sizeof(char *))
static bool parse(TiredRequest *request, TiredError *error, const char *const *args, size_t count)
{
    return tired_cli_parse((int)count, args, request, error);
}
int main(void)
{
    TiredRequest request = {0};
    TiredError error = {0};
    CHECK(PARSE("tired", "./server", "--name", "workload-name", "--help", ""));
    CHECK(request.command == TIRED_COMMAND_CREATE && !request.help);
    CHECK(request.arguments.count == 5 && strcmp(request.arguments.items[1].data, "--name") == 0);
    CHECK(request.arguments.items[4].length == 0);
    CHECK(PARSE("tired", "--name", "relay", "./server", "--json"));
    CHECK(!request.json && request.seen[TIRED_FIELD_NAME]);
    CHECK(strcmp(request.overrides.fields[TIRED_FIELD_NAME].value.text.data, "relay") == 0);
    CHECK(PARSE("tired", "--user", "status", "relay"));
    CHECK(request.command == TIRED_COMMAND_STATUS);
    CHECK(tired_spec_choice_is(&request.overrides, TIRED_FIELD_SCOPE, "user"));
    CHECK(PARSE("tired", "--", "status", "--help"));
    CHECK(request.command == TIRED_COMMAND_CREATE && request.arguments.count == 2 &&
          request.explicit_boundary);
    CHECK(PARSE("tired", "create", "--", "status"));
    CHECK(request.command == TIRED_COMMAND_CREATE);
    CHECK(
        PARSE("tired", "plan", "--offline", "--unit", "--nofile", "1024:4096", "--", "/bin/true"));
    CHECK(request.command == TIRED_COMMAND_PLAN && request.offline && request.unit);
    CHECK(request.overrides.fields[TIRED_FIELD_NOFILE_HARD].value.limit.value == 4096);
    CHECK(PARSE("tired", "create", "--dry-run", "--offline", "./server"));
    CHECK(request.command == TIRED_COMMAND_PLAN);
    CHECK(PARSE("tired", "plan", "--working-directory", "relative dir", "--env", "X=1", "--env",
                "X=2", "--", "./server"));
    CHECK(strcmp(request.working_directory.data, "relative dir") == 0);
    CHECK(strcmp(tired_environment_find(&request.environment, "X", 1)->value.data, "2") == 0);
    CHECK(!PARSE("tired", "--system", "--user", "./server"));
    CHECK(!PARSE("tired", "--name", "one", "--set", "name=two", "./server"));
    CHECK(!PARSE("tired", "--set", "restart=always", "--restart", "no", "./server"));
    CHECK(!PARSE("tired", "--name"));
    CHECK(!PARSE("tired", "--unknown", "./server"));
    CHECK(!PARSE("tired", "--yes=false", "./server"));
    CHECK(!PARSE("tired", "create", "--offline", "./server"));
    CHECK(!PARSE("tired", "plan", "--unit", "--json", "./server"));
    CHECK(!PARSE("tired", "--set", "ExecStartPre=evil", "./server"));
    CHECK(!PARSE("tired", "--name", "relay", "status", "relay"));
    CHECK(!PARSE("tired", "--working-directory", "", "./server"));
    CHECK(!PARSE("tired", "plan", "--include-sensitive", "./server"));
    CHECK(!PARSE("tired", "--no-start", "--start", "./server"));
    CHECK(PARSE("tired", "--unset", "restart", "./server"));
    CHECK(request.overrides.fields[TIRED_FIELD_RESTART].inherit);
    CHECK(!PARSE("tired", "--unset", "restart", "--restart", "always", "./server"));
    CHECK(!PARSE("tired", "--set", "restart=always", "--unset", "restart", "./server"));
    CHECK(!PARSE("tired", "--unset", "argv", "./server"));
    CHECK(!PARSE("tired", "--unset", "environment_files", "--env-file", "env", "./server"));
    CHECK(!PARSE("tired", "--env-file", "env", "--unset", "environment_files", "./server"));
    CHECK(!PARSE("tired", "rename", "old"));
    CHECK(PARSE("tired", "rename", "old", "new"));
    CHECK(PARSE("tired", "profiles", "explain", "--profile", "backhaul", "--json", "--",
                "/bin/true", "--help", "--json"));
    CHECK(request.profile_explain && request.json && !request.help);
    CHECK(request.arguments.count == 3 && strcmp(request.arguments.items[1].data, "--help") == 0);
    CHECK(!PARSE("tired", "profiles", "explain", "/bin/true"));
    CHECK(!PARSE("tired", "profiles", "explain", "--"));
    CHECK(!PARSE("tired", "profiles", "explain", "--output", "file", "--", "/bin/true"));
    CHECK(PARSE("tired", "--help"));
    CHECK(request.help);
    CHECK(PARSE("tired"));
    CHECK(request.command == TIRED_COMMAND_DASHBOARD);
    tired_request_destroy(&request);
    tired_request_destroy(&request);
    return 0;
}
