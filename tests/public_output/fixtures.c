#define _GNU_SOURCE
#include "tired/io.h"
#include "tired/json.h"
#include "tired/mutation.h"
#include "tired/process.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CORPUS_LIMIT TIRED_INPUT_LIMIT
typedef struct
{
    const char *name;
    int status;
} Expected;
static const Expected expected[] = {{"dashboard", 0},
                                    {"shorthand-create", 0},
                                    {"create", 0},
                                    {"plan-offline", 0},
                                    {"plan-live", 0},
                                    {"dry-run", 0},
                                    {"list", 0},
                                    {"status", 0},
                                    {"show", 0},
                                    {"show-effective", 0},
                                    {"logs", 0},
                                    {"stop", 0},
                                    {"start", 0},
                                    {"restart", 0},
                                    {"enable", 0},
                                    {"disable", 0},
                                    {"enable-now", 0},
                                    {"disable-now", 0},
                                    {"edit", 0},
                                    {"edit-defer", 0},
                                    {"rename", 0},
                                    {"remove", 0},
                                    {"doctor", 0},
                                    {"doctor-service", 0},
                                    {"recover", 0},
                                    {"recover-finish", 0},
                                    {"recover-rollback", 0},
                                    {"profiles-list", 0},
                                    {"profiles-show", 0},
                                    {"profiles-explain", 0},
                                    {"profiles-validate", 0},
                                    {"profiles-install", 0},
                                    {"profiles-remove", 0},
                                    {"config-show", 0},
                                    {"config-validate", 0},
                                    {"version", 0},
                                    {"build-info", 0},
                                    {"help", 0},
                                    {"dashboard-refusal", 2},
                                    {"shorthand-create-refusal", 2},
                                    {"create-conflict", 5},
                                    {"create-runtime-failure", 7},
                                    {"plan-refusal", 2},
                                    {"list-refusal", 2},
                                    {"status-missing", 9},
                                    {"status-inactive", 7},
                                    {"show-missing", 9},
                                    {"logs-empty", 0},
                                    {"logs-refusal", 2},
                                    {"start-missing", 9},
                                    {"stop-missing", 9},
                                    {"restart-missing", 9},
                                    {"enable-missing", 9},
                                    {"disable-missing", 9},
                                    {"edit-missing", 9},
                                    {"edit-rolled-back", 6},
                                    {"rename-missing", 9},
                                    {"remove-missing", 9},
                                    {"doctor-missing", 9},
                                    {"recover-pending", 8},
                                    {"recover-finish-missing", 9},
                                    {"recover-rollback-missing", 9},
                                    {"profiles-list-refusal", 2},
                                    {"profiles-show-missing", 9},
                                    {"profiles-explain-refusal", 2},
                                    {"profiles-validate-refusal", 2},
                                    {"profiles-install-refusal", 2},
                                    {"profiles-remove-missing", 9},
                                    {"config-show-refusal", 2},
                                    {"config-validate-refusal", 2},
                                    {"version-refusal", 2},
                                    {"build-info-refusal", 2},
                                    {"help-refusal", 2},
                                    {"cleanup-shorthand", 0},
                                    {"cleanup-failed-create", 0},
                                    {"cleanup-start", 0},
                                    {"cleanup-recovered-create", 0}};
static bool envelope(const char *data, size_t length, int status, bool events, TiredError *error)
{
    size_t offset = 0, count = 0;
    do
    {
        size_t end = length;
        if (events)
        {
            const char *newline = memchr(data + offset, '\n', length - offset);
            if (newline != NULL)
                end = (size_t)(newline - data);
        }
        struct json_object *object = NULL, *version = NULL, *exit_code = NULL, *event = NULL;
        bool ok =
            tired_json_parse(data + offset, end - offset, TIRED_INPUT_LIMIT, &object, error) &&
            json_object_is_type(object, json_type_object) &&
            json_object_object_get_ex(object, "schema_version", &version) &&
            json_object_is_type(version, json_type_int) && json_object_get_int(version) == 1;
        if (ok && json_object_object_get_ex(object, "exit_code", &exit_code))
            ok = json_object_is_type(exit_code, json_type_int) &&
                 json_object_get_int(exit_code) == status;
        if (ok && events)
            ok = json_object_object_get_ex(object, "event_type", &event) &&
                 json_object_is_type(event, json_type_string);
        json_object_put(object);
        if (!ok)
            return false;
        ++count;
        offset = end + (events && end < length ? 1U : 0U);
    } while (events && offset < length);
    return count != 0;
}
static bool validate(const char *path, TiredError *error)
{
    TiredText bytes = {0};
    struct json_object *document = NULL, *cases = NULL;
    bool seen[sizeof(expected) / sizeof(expected[0])] = {0};
    bool ok = tired_read_file(path, CORPUS_LIMIT, &bytes, error) &&
              tired_json_parse(bytes.data, bytes.length, CORPUS_LIMIT, &document, error) &&
              json_object_is_type(document, json_type_object) &&
              json_object_object_get_ex(document, "cases", &cases) &&
              json_object_is_type(cases, json_type_array) &&
              json_object_array_length(cases) == sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0; ok && i < json_object_array_length(cases); ++i)
    {
        struct json_object *row = json_object_array_get_idx(cases, i), *name = NULL, *status = NULL,
                           *output = NULL, *arguments = NULL, *events = NULL, *diagnostic = NULL;
        ok = json_object_object_get_ex(row, "name", &name) &&
             json_object_is_type(name, json_type_string) &&
             json_object_object_get_ex(row, "exit_code", &status) &&
             json_object_is_type(status, json_type_int) &&
             json_object_object_get_ex(row, "stdout", &output) &&
             json_object_is_type(output, json_type_string) &&
             json_object_object_get_ex(row, "stderr", &diagnostic) &&
             json_object_is_type(diagnostic, json_type_string) &&
             json_object_object_get_ex(row, "argv", &arguments) &&
             json_object_is_type(arguments, json_type_array) &&
             json_object_array_length(arguments) > 1 &&
             json_object_object_get_ex(row, "ndjson", &events) &&
             json_object_is_type(events, json_type_boolean);
        size_t selected = sizeof(expected) / sizeof(expected[0]);
        for (size_t j = 0; ok && j < sizeof(expected) / sizeof(expected[0]); ++j)
            if (strcmp(json_object_get_string(name), expected[j].name) == 0)
                selected = j;
        ok = ok && selected < sizeof(expected) / sizeof(expected[0]) && !seen[selected] &&
             json_object_get_int(status) == expected[selected].status;
        if (ok)
        {
            seen[selected] = true;
            ok =
                envelope(json_object_get_string(output), (size_t)json_object_get_string_len(output),
                         expected[selected].status, json_object_get_boolean(events), error);
        }
        if (!ok)
            fprintf(stderr, "Invalid public output fixture at index %zu.\n", i);
    }
    json_object_put(document);
    tired_text_destroy(&bytes);
    return ok;
}
static char *const environment[] = {"PATH=/usr/local/bin:/usr/bin:/bin", "LC_ALL=C", "TERM=dumb",
                                    "HOME=/root", NULL};
static bool capture(struct json_object *cases, const char *name, int status, bool events,
                    char *const arguments[], TiredError *error)
{
    TiredProcess *process = NULL;
    if (!tired_process_start(arguments[0], arguments, environment, 120000, TIRED_INPUT_LIMIT,
                             &process, error))
        return false;
    while (!tired_process_step(process))
    {
        struct timespec pause = {.tv_nsec = 10000000};
        (void)nanosleep(&pause, NULL);
    }
    TiredProcessResult result = tired_process_result(process);
    bool ok = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == status &&
              envelope(result.standard_output, result.output_length, status, events, error);
    struct json_object *row = json_object_new_object(), *argv = json_object_new_array();
    if (row == NULL || argv == NULL)
        ok = false;
    if (ok)
    {
        for (size_t i = 0; arguments[i] != NULL; ++i)
            json_object_array_add(argv, json_object_new_string(arguments[i]));
        json_object_object_add(row, "name", json_object_new_string(name));
        json_object_object_add(row, "argv", argv);
        argv = NULL;
        json_object_object_add(row, "exit_code", json_object_new_int(result.exit_code));
        json_object_object_add(row, "ndjson", json_object_new_boolean(events));
        json_object_object_add(
            row, "stdout",
            json_object_new_string_len(result.standard_output, (int)result.output_length));
        json_object_object_add(
            row, "stderr",
            json_object_new_string_len(result.standard_error, (int)result.error_length));
        ok = json_object_array_add(cases, row) == 0;
        if (ok)
            row = NULL;
    }
    if (!ok)
        fprintf(stderr, "Capture %s failed: exit=%d expected=%d\n%.*s\n%.*s\n", name,
                result.exit_code, status, (int)result.output_length, result.standard_output,
                (int)result.error_length, result.standard_error);
    json_object_put(row);
    json_object_put(argv);
    tired_process_destroy(process);
    return ok;
}
static void preparation_crash(void *context, const char *phase)
{
    (void)context;
    if (strcmp(phase, "prepared") == 0)
    {
        (void)raise(SIGKILL);
        _exit(98);
    }
}
static bool prepare_interrupted(const char *name, char uuid[37], TiredError *error)
{
    TiredRequest request = {0};
    TiredPlan plan = {0};
    TiredSettings settings = {0};
    TiredLayout layout = {0};
    TiredMutation mutation = {0};
    TiredTextList risks = {0};
    const char *arguments[] = {"tired",       "--name", name,
                               "--profile",   "none",   "--no-start",
                               "--no-enable", "--",     "/opt/tired-tests/tired_fixture",
                               "loop"};
    tired_settings_defaults(&settings);
    bool ok = tired_layout_discover(false, &layout, error) &&
              tired_cli_parse((int)(sizeof(arguments) / sizeof(arguments[0])), arguments, &request,
                              error) &&
              tired_plan_prepare(&request, &plan, error) &&
              tired_text_list_append(&risks, "run-as-root", 11, 16, 4096, error) &&
              tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, error);
    if (ok)
    {
        memcpy(uuid, mutation.proposed.metadata.transaction_uuid, 37);
        pid_t child = fork();
        ok = child >= 0;
        if (child == 0)
        {
            TiredNativeBackend *native = NULL;
            TiredBackend backend = {0};
            TiredOperationResult result = {0};
            if (!tired_backend_open(&layout, &native, &backend, error))
                _exit(97);
            backend.tick = preparation_crash;
            (void)tired_mutation_apply(&mutation, &layout, &backend, &result, error);
            _exit(98);
        }
        if (ok)
        {
            int status;
            ok = waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
                 WTERMSIG(status) == SIGKILL;
        }
    }
    tired_mutation_destroy(&mutation);
    tired_layout_destroy(&layout);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    tired_settings_destroy(&settings);
    tired_text_list_destroy(&risks);
    return ok;
}
#define RECORD(name, status, events, ...)                                                          \
    do                                                                                             \
    {                                                                                              \
        char *arguments[] = {frontend, __VA_ARGS__, NULL};                                         \
        if (!capture(cases, name, status, events, arguments, &error))                              \
            goto cleanup;                                                                          \
    } while (0)
static bool record(char *frontend, const char *source, const char *destination)
{
    TiredError error = {0};
    TiredText profile = {0}, os_release = {0};
    struct json_object *document = json_object_new_object(), *cases = json_object_new_array(),
                       *profile_document = NULL, *host = json_object_new_object();
    bool ok = false;
    char profile_source[4096], defaults[4096], fixture[] = "/opt/tired-tests/output-XXXXXX",
                                               profile_path[4096], invalid_path[4096], uuid[37];
    if (document == NULL || cases == NULL || host == NULL || mkdtemp(fixture) == NULL ||
        snprintf(profile_source, sizeof(profile_source), "%s/profiles/generic.json", source) >=
            (int)sizeof(profile_source) ||
        snprintf(defaults, sizeof(defaults), "%s/defaults.json", source) >= (int)sizeof(defaults) ||
        snprintf(profile_path, sizeof(profile_path), "%s/profile.json", fixture) >=
            (int)sizeof(profile_path) ||
        snprintf(invalid_path, sizeof(invalid_path), "%s/invalid.json", fixture) >=
            (int)sizeof(invalid_path) ||
        !tired_read_file(profile_source, TIRED_INPUT_LIMIT, &profile, &error) ||
        !tired_json_parse(profile.data, profile.length, TIRED_INPUT_LIMIT, &profile_document,
                          &error))
        goto cleanup;
    json_object_object_add(profile_document, "id",
                           json_object_new_string("tired-qualification-output-profile"));
    const char *encoded = json_object_to_json_string_ext(profile_document, JSON_C_TO_STRING_PRETTY);
    if (encoded == NULL ||
        !tired_write_private_new(profile_path, encoded, strlen(encoded), &error) ||
        !tired_write_private_new(invalid_path, "{}", 2, &error))
        goto cleanup;
    struct utsname identity;
    if (uname(&identity) != 0 || !tired_read_file("/etc/os-release", 65536, &os_release, &error))
        goto cleanup;
    json_object_object_add(host, "architecture", json_object_new_string(identity.machine));
    json_object_object_add(host, "kernel", json_object_new_string(identity.release));
    json_object_object_add(host, "os_release", json_object_new_string(os_release.data));
    json_object_object_add(document, "format_version", json_object_new_int(1));
    json_object_object_add(document, "host", host);
    host = NULL;
    /* Inspection first also rejects a guest whose earlier work remains unresolved. */
    RECORD("recover", 0, false, "recover", "--json");
    RECORD("version", 0, false, "--version", "--json");
    RECORD("build-info", 0, false, "--build-info", "--json");
    RECORD("help", 0, false, "--help", "--json");
    RECORD("dashboard", 0, false, "--json");
    RECORD("plan-offline", 0, false, "plan", "--offline", "--profile", "none", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("plan-live", 0, false, "plan", "--profile", "none", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("dry-run", 0, false, "create", "--dry-run", "--profile", "none", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("shorthand-create", 0, false, "--name", "tired-qualification-output-short", "--profile",
           "none", "--no-enable", "--yes", "--allow-risk", "run-as-root", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("cleanup-shorthand", 0, false, "remove", "tired-qualification-output-short", "--yes",
           "--json");
    RECORD("create", 0, false, "create", "--name", "tired-qualification-output", "--profile",
           "none", "--no-enable", "--yes", "--allow-risk", "run-as-root", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("create-conflict", 5, false, "create", "--name", "tired-qualification-output",
           "--profile", "none", "--no-enable", "--yes", "--allow-risk", "run-as-root", "--json",
           "--", "/opt/tired-tests/tired_fixture", "loop");
    RECORD("list", 0, false, "list", "--json");
    RECORD("status", 0, false, "status", "tired-qualification-output", "--json");
    RECORD("show", 0, false, "show", "tired-qualification-output", "--json");
    RECORD("show-effective", 0, false, "show", "tired-qualification-output", "--effective",
           "--json");
    RECORD("logs", 0, true, "logs", "tired-qualification-output", "--lines", "2", "--json");
    RECORD("doctor", 0, false, "doctor", "--json");
    RECORD("doctor-service", 0, false, "doctor", "tired-qualification-output", "--json");
    RECORD("stop", 0, false, "stop", "tired-qualification-output", "--json");
    RECORD("status-inactive", 7, false, "status", "tired-qualification-output", "--check-active",
           "--json");
    RECORD("start", 0, false, "start", "tired-qualification-output", "--json");
    RECORD("restart", 0, false, "restart", "tired-qualification-output", "--json");
    RECORD("enable", 0, false, "enable", "tired-qualification-output", "--json");
    RECORD("disable", 0, false, "disable", "tired-qualification-output", "--json");
    RECORD("enable-now", 0, false, "enable", "tired-qualification-output", "--now", "--json");
    RECORD("disable-now", 0, false, "disable", "tired-qualification-output", "--now", "--json");
    RECORD("cleanup-start", 0, false, "start", "tired-qualification-output", "--json");
    RECORD("edit", 0, false, "edit", "tired-qualification-output", "--description",
           "Output fixture", "--yes", "--json");
    RECORD("edit-defer", 0, false, "edit", "tired-qualification-output", "--description",
           "Deferred output fixture", "--apply-mode", "defer", "--yes", "--json");
    RECORD("edit-rolled-back", 6, false, "edit", "tired-qualification-output", "--yes", "--json",
           "--", "/opt/tired-tests/tired_fixture", "exit", "1");
    RECORD("rename", 0, false, "rename", "tired-qualification-output",
           "tired-qualification-output-renamed", "--yes", "--json");
    RECORD("remove", 0, false, "remove", "tired-qualification-output-renamed", "--yes", "--json");
    RECORD("create-runtime-failure", 7, false, "create", "--name",
           "tired-qualification-output-failed", "--profile", "none", "--no-enable", "--yes",
           "--allow-risk", "run-as-root", "--json", "--", "/opt/tired-tests/tired_fixture", "exit",
           "1");
    RECORD("cleanup-failed-create", 0, false, "remove", "tired-qualification-output-failed",
           "--yes", "--json");
    RECORD("profiles-list", 0, false, "profiles", "list", "--json");
    RECORD("profiles-show", 0, false, "profiles", "show", "generic", "--json");
    RECORD("profiles-explain", 0, false, "profiles", "explain", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("profiles-validate", 0, false, "profiles", "validate", profile_path, "--json");
    RECORD("profiles-install", 0, false, "profiles", "install", profile_path, "--yes", "--json");
    RECORD("profiles-remove", 0, false, "profiles", "remove", "tired-qualification-output-profile",
           "--yes", "--json");
    RECORD("config-show", 0, false, "config", "show", "--json");
    RECORD("config-validate", 0, false, "config", "validate", defaults, "--json");
    if (!prepare_interrupted("tired-qualification-output-finish", uuid, &error))
        goto cleanup;
    RECORD("recover-pending", 8, false, "recover", "--json");
    RECORD("recover-finish", 0, false, "recover", "--transaction", uuid, "--resolution", "finish",
           "--yes", "--json");
    RECORD("cleanup-recovered-create", 0, false, "remove", "tired-qualification-output-finish",
           "--yes", "--json");
    if (!prepare_interrupted("tired-qualification-output-rollback", uuid, &error))
        goto cleanup;
    RECORD("recover-rollback", 0, false, "recover", "--transaction", uuid, "--resolution",
           "rollback", "--yes", "--json");
    const char *missing = "tired-qualification-output-missing";
    RECORD("logs-empty", 0, true, "logs", (char *)missing, "--json");
    RECORD("logs-refusal", 2, true, "logs", "../outside", "--json");
    RECORD("dashboard-refusal", 2, false, "--json", "--invalid-output-fixture-option");
    RECORD("shorthand-create-refusal", 2, false, "--name", "tired-qualification-output-refused",
           "--profile", "none", "--json", "--", "/opt/tired-tests/tired_fixture", "loop");
    RECORD("plan-refusal", 2, false, "plan", "--type", "invalid", "--json", "--",
           "/opt/tired-tests/tired_fixture", "loop");
    RECORD("list-refusal", 2, false, "list", "extra", "--json");
    const char *actions[] = {"status", "show",    "start", "stop",   "restart",
                             "enable", "disable", "edit",  "remove", "doctor"};
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
    {
        char id[48];
        (void)snprintf(id, sizeof(id), "%s-missing", actions[i]);
        RECORD(id, 9, strcmp(actions[i], "logs") == 0, (char *)actions[i], (char *)missing, "--yes",
               "--json");
    }
    RECORD("rename-missing", 9, false, "rename", (char *)missing,
           "tired-qualification-output-missing-new", "--yes", "--json");
    const char *absent_uuid = "11111111-1111-4111-8111-111111111111";
    RECORD("recover-finish-missing", 9, false, "recover", "--transaction", (char *)absent_uuid,
           "--resolution", "finish", "--yes", "--json");
    RECORD("recover-rollback-missing", 9, false, "recover", "--transaction", (char *)absent_uuid,
           "--resolution", "rollback", "--yes", "--json");
    RECORD("profiles-list-refusal", 2, false, "profiles", "list", "extra", "--json");
    RECORD("profiles-show-missing", 9, false, "profiles", "show", (char *)missing, "--json");
    RECORD("profiles-explain-refusal", 2, false, "profiles", "explain", "--json", "--");
    RECORD("profiles-validate-refusal", 2, false, "profiles", "validate", invalid_path, "--json");
    RECORD("profiles-install-refusal", 2, false, "profiles", "install", profile_path, "--json");
    RECORD("profiles-remove-missing", 9, false, "profiles", "remove", (char *)missing, "--yes",
           "--json");
    RECORD("config-show-refusal", 2, false, "config", "show", "extra", "--json");
    RECORD("config-validate-refusal", 2, false, "config", "validate", invalid_path, "--json");
    RECORD("version-refusal", 2, false, "--version", "--json", "--invalid-output-fixture-option");
    RECORD("build-info-refusal", 2, false, "--build-info", "--json",
           "--invalid-output-fixture-option");
    RECORD("help-refusal", 2, false, "--help", "--json", "--invalid-output-fixture-option");
    json_object_object_add(document, "cases", cases);
    cases = NULL;
    encoded = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PRETTY);
    ok = encoded != NULL && strlen(encoded) <= CORPUS_LIMIT &&
         tired_write_private_new(destination, encoded, strlen(encoded), &error) &&
         validate(destination, &error);
cleanup:
    if (!ok && error.code != NULL)
        fprintf(stderr, "%s: %s\n", error.code, error.message);
    json_object_put(document);
    json_object_put(cases);
    json_object_put(profile_document);
    json_object_put(host);
    tired_text_destroy(&profile);
    tired_text_destroy(&os_release);
    return ok;
}
int main(int argc, char **argv)
{
    TiredError error = {0};
    if (argc == 3 && strcmp(argv[1], "validate") == 0)
        return validate(argv[2], &error) ? 0 : 1;
    struct stat marker;
    if (argc != 5 || strcmp(argv[1], "record") != 0 || argv[2][0] != '/' || argv[3][0] != '/' ||
        argv[4][0] != '/' || getuid() != 0 || lstat("/opt/tired-tests/disposable", &marker) != 0 ||
        !S_ISREG(marker.st_mode) || marker.st_uid != 0 || (marker.st_mode & 0022) != 0)
        return 3;
    return chdir("/opt/tired-tests") == 0 && record(argv[2], argv[3], argv[4]) ? 0 : 1;
}
