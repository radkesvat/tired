#include "tired/cli.h"
#include "tired/list_frontend.h"
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
static bool presentation(const char *const *arguments, size_t count, bool expected)
{
    TiredRequest request = {0};
    TiredError error = {0};
    bool json = !expected;
    (void)tired_cli_parse_format((int)count, arguments, &request, &json, &error);
    tired_request_destroy(&request);
    return json == expected;
}
#define PRESENTATION(expected, ...)                                                                \
    presentation((const char *const[]){__VA_ARGS__},                                               \
                 sizeof((const char *const[]){__VA_ARGS__}) / sizeof(char *), expected)
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
    CHECK(PARSE("tired", "--hardening", "baseline", "./server"));
    CHECK(tired_spec_uses_hardening_baseline(&request.overrides));
    CHECK(request.seen[TIRED_FIELD_PRIVATE_TMP] && request.seen[TIRED_FIELD_PROTECT_SYSTEM] &&
          request.overrides.fields[TIRED_FIELD_NO_NEW_PRIVILEGES].origin == TIRED_ORIGIN_USER);
    CHECK(!PARSE("tired", "--hardening", "unknown", "./server"));
    CHECK(!PARSE("tired", "--hardening", "baseline", "--hardening", "baseline", "./server"));
    CHECK(!PARSE("tired", "--hardening", "baseline", "--set", "private_tmp=false", "./server"));
    CHECK(!PARSE("tired", "--unset", "private_tmp", "--hardening", "baseline", "./server"));
    CHECK(PARSE("tired", "./server", "--hardening", "baseline"));
    CHECK(!request.seen[TIRED_FIELD_PRIVATE_TMP] && request.arguments.count == 3);
    CHECK(PARSE("tired", "plan", "--offline", "--user", "--enable-linger", "--", "./server"));
    CHECK(request.overrides.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean);
    CHECK(PARSE("tired", "--user", "--enable-linger", "./server"));
    CHECK(request.overrides.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean &&
          request.command == TIRED_COMMAND_CREATE);
    CHECK(PARSE("tired", "edit", "relay", "--user", "--enable-linger"));
    CHECK(!PARSE("tired", "--enable-linger", "./server"));
    CHECK(!PARSE("tired", "plan", "--offline", "--system", "--enable-linger", "./server"));
    CHECK(!PARSE("tired", "plan", "--offline", "--user", "--enable-linger", "--unit", "./server"));
    CHECK(!PARSE("tired", "plan", "--offline", "--user", "--enable-linger", "--enable-linger",
                 "./server"));
    CHECK(!PARSE("tired", "plan", "--offline", "--user", "--enable-linger=true", "./server"));
    CHECK(!PARSE("tired", "status", "relay", "--user", "--enable-linger"));
    CHECK(!PARSE("tired", "profiles", "explain", "--user", "--enable-linger", "--", "./server"));
    CHECK(PARSE("tired", "./server", "--enable-linger"));
    CHECK(!tired_field_has_value(&request.overrides.fields[TIRED_FIELD_ENABLE_LINGER]) &&
          request.arguments.count == 2);
    CHECK(!PARSE("tired", "--name", "one", "--set", "name=two", "./server"));
    CHECK(!PARSE("tired", "--set", "restart=always", "--restart", "no", "./server"));
    CHECK(!PARSE("tired", "--name"));
    CHECK(!PARSE("tired", "--unknown", "./server"));
    CHECK(PARSE("tired", "--sensitive-arg", "1", "--sensitive-arg", "1", "./server", "value"));
    CHECK(request.sensitive_arguments[1]);
    CHECK(!PARSE("tired", "--sensitive-arg", "0", "./server", "value"));
    CHECK(!PARSE("tired", "--sensitive-arg", "2", "./server", "value"));
    CHECK(!PARSE("tired", "--sensitive-arg", "-1", "./server", "value"));
    CHECK(PARSE("tired", "--sensitive-arg", "1", "edit", "service"));
    CHECK(!PARSE("tired", "--allow-risk", "invented-risk", "./server"));
    CHECK(PARSE("tired", "--allow-risk", "run-as-root", "./server"));
    CHECK(!PARSE("tired", "--yes=false", "./server"));
    CHECK(!PARSE("tired", "create", "--offline", "./server"));
    CHECK(!PARSE("tired", "plan", "--unit", "--json", "./server"));
    CHECK(!PARSE("tired", "--set", "ExecStartPre=evil", "./server"));
    CHECK(!PARSE("tired", "--name", "relay", "status", "relay"));
    CHECK(PARSE("tired", "status", "relay", "--check-active", "--json"));
    CHECK(request.check_active && request.json && request.command == TIRED_COMMAND_STATUS);
    CHECK(!PARSE("tired", "status", "relay", "--check-active", "--check-active"));
    CHECK(!PARSE("tired", "status", "relay", "--check-active=true"));
    CHECK(!PARSE("tired", "list", "--check-active"));
    CHECK(PARSE("tired", "logs", "relay", "--follow", "--lines=0", "--boot", "current", "--since",
                "@0.000001", "--json"));
    CHECK(request.logs.follow && request.logs.lines_set && request.logs.lines == 0 &&
          request.logs.boot_set && request.logs.boot_current && request.logs.since_set &&
          request.logs.since_usec == 1 && request.json);
    CHECK(PARSE("tired", "--user", "logs", "relay", "--lines", "10000", "--boot",
                "0123456789ABCDEF0123456789ABCDEF"));
    CHECK(request.logs.lines == 10000 && !request.logs.boot_current &&
          strcmp(request.logs.boot_id, "0123456789abcdef0123456789abcdef") == 0);
    CHECK(PARSE("tired", "logs", "relay"));
    CHECK(!request.logs.lines_set && !request.logs.since_set && !request.logs.boot_set &&
          !request.logs.follow);
    CHECK(!PARSE("tired", "logs", "relay", "--lines", "10001"));
    CHECK(!PARSE("tired", "logs", "relay", "--lines", "-1"));
    CHECK(!PARSE("tired", "logs", "relay", "--lines", "1", "--lines", "2"));
    CHECK(!PARSE("tired", "logs", "relay", "--follow", "--follow"));
    CHECK(!PARSE("tired", "logs", "relay", "--follow=true"));
    CHECK(!PARSE("tired", "logs", "relay", "--boot", "current", "--boot", "current"));
    CHECK(!PARSE("tired", "logs", "relay", "--boot", "123"));
    CHECK(!PARSE("tired", "logs", "relay", "--boot", "g123456789abcdef0123456789abcdef"));
    CHECK(!PARSE("tired", "logs", "relay", "--since", "@0", "--since", "@1"));
    CHECK(!PARSE("tired", "status", "relay", "--follow"));
    CHECK(!PARSE("tired", "list", "--lines", "5"));
    CHECK(!PARSE("tired", "show", "relay", "--since", "@0"));
    CHECK(!PARSE("tired", "plan", "--boot", "current", "--", "./app"));
    CHECK(PARSE("tired", "./app", "--follow", "--since", "bad"));
    CHECK(request.arguments.count == 4 && !request.logs.follow);
    uint64_t since = 99;
    CHECK(tired_log_since_parse("1970-01-01T00:00:00Z", &since, &error) && since == 0);
    CHECK(tired_log_since_parse("1970-01-02T01:02:03.4Z", &since, &error) &&
          since == UINT64_C(90123400000));
    CHECK(tired_log_since_parse("2000-02-29T00:00:00Z", &since, &error) &&
          since == UINT64_C(951782400000000));
    CHECK(tired_log_since_parse("@18446744073709.551615", &since, &error) && since == UINT64_MAX);
    const char *bad_times[] = {"",
                               "@",
                               "@-1",
                               "@+1",
                               "@1.",
                               "@1.1234567",
                               "@18446744073709.551616",
                               "@18446744073710",
                               "@18446744073709551616",
                               "now",
                               "2026-09-29",
                               "2026-09-29T00:00:00+00:00",
                               "1969-12-31T23:59:59Z",
                               "2001-02-29T00:00:00Z",
                               "2100-02-29T00:00:00Z",
                               "2024-04-31T00:00:00Z",
                               "2024-00-01T00:00:00Z",
                               "2024-13-01T00:00:00Z",
                               "2024-01-00T00:00:00Z",
                               "2024-01-01T24:00:00Z",
                               "2024-01-01T00:60:00Z",
                               "2024-01-01T00:00:60Z",
                               "2024-01-01T00:00:00.Z",
                               "@0x1",
                               " @1"};
    for (size_t i = 0; i < sizeof(bad_times) / sizeof(bad_times[0]); ++i)
    {
        since = 99;
        CHECK(!tired_log_since_parse(bad_times[i], &since, &error) && since == 99);
    }
    CHECK(PARSE("tired", "list", "--active-state", "active", "--enabled-state=enabled", "--profile",
                "generic", "--search", "ReLaY"));
    CHECK(tired_list_match(&request, "relay.service", "active", "enabled", "generic") ==
          TIRED_LIST_MATCH);
    CHECK(tired_list_match(&request, "RELAY.service", NULL, "enabled", "generic") ==
          TIRED_LIST_UNKNOWN);
    CHECK(tired_list_match(&request, "other.service", NULL, "enabled", "generic") ==
          TIRED_LIST_NO_MATCH);
    CHECK(tired_list_match(&request, "relay.service", "failed", "enabled", "generic") ==
          TIRED_LIST_NO_MATCH);
    CHECK(tired_list_match(&request, NULL, NULL, NULL, NULL) == TIRED_LIST_UNKNOWN);
    CHECK(!PARSE("tired", "list", "--search", ""));
    CHECK(!PARSE("tired", "list", "--search", "bad\ntext"));
    CHECK(!PARSE("tired", "list", "--active-state", "active", "--active-state", "failed"));
    CHECK(!PARSE("tired", "status", "relay", "--search", "relay"));
    CHECK(!PARSE("tired", "show", "relay", "--profile", "generic"));
    CHECK(PARSE("tired", "show", "relay", "--effective", "--unit"));
    CHECK(request.effective && request.unit);
    CHECK(!PARSE("tired", "show", "relay", "--effective", "--effective"));
    CHECK(!PARSE("tired", "status", "relay", "--effective"));
    CHECK(!PARSE("tired", "list", "--profile", "generic", "--env", "KEY=value"));
    CHECK(PARSE("tired", "--profile", "generic", "list"));
    CHECK(tired_list_match(&request, "relay.service", NULL, NULL, "none") == TIRED_LIST_NO_MATCH);
    CHECK(PARSE("tired", "./app", "--check-active"));
    CHECK(!request.check_active && request.arguments.count == 2);
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
    CHECK(PARSE("tired", "edit", "old", "--yes", "--sensitive-arg", "2", "--", "/bin/echo", "",
                "--json"));
    CHECK(request.arguments.count == 1 && request.replacement.count == 3 && !request.json);
    CHECK(request.replacement.items[1].length == 0 && request.sensitive_arguments[2]);
    CHECK(!PARSE("tired", "edit", "old", "--"));
    CHECK(!PARSE("tired", "edit", "--", "/bin/echo"));
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
    CHECK(PRESENTATION(true, "tired", "plan", "--type", "invalid", "--json", "--", "/bin/true"));
    CHECK(PRESENTATION(true, "tired", "--unknown", "--json"));
    CHECK(PRESENTATION(true, "tired", "--json", "--type"));
    CHECK(PRESENTATION(true, "tired", "--set", "invented=value", "--json", "/bin/true"));
    CHECK(PRESENTATION(true, "tired", "status", "relay", "--type", "invalid", "--json"));
    CHECK(PRESENTATION(true, "tired", "profiles", "explain", "--type", "invalid", "--json", "--",
                       "/bin/true"));
    CHECK(PRESENTATION(false, "tired", "--description", "--json", "/bin/true"));
    CHECK(PRESENTATION(false, "tired", "--type", "--json", "/bin/true"));
    CHECK(PRESENTATION(false, "tired", "--set", "description=--json", "/bin/true"));
    CHECK(PRESENTATION(false, "tired", "plan", "--type", "invalid", "/bin/true", "--json"));
    CHECK(PRESENTATION(false, "tired", "--unknown", "/bin/true", "--json"));
    CHECK(PRESENTATION(false, "tired", "create", "--", "/bin/true", "--json"));
    CHECK(PRESENTATION(false, "tired", "edit", "relay", "--", "/bin/true", "--json"));
    CHECK(PRESENTATION(false, "tired", "profiles", "explain", "--", "/bin/true", "--json"));
    CHECK(PRESENTATION(true, "tired", "--description", "--json", "--json", "/bin/true"));
    tired_request_destroy(&request);
    tired_request_destroy(&request);
    return 0;
}
