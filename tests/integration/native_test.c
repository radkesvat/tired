#define _POSIX_C_SOURCE 200809L
#include "tired/backend.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/process.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%d: %s [%s]\n", __LINE__, #expression,                                \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static char *frontend = "/usr/local/bin/tired";
static char *environment[] = {"PATH=/usr/local/bin:/usr/bin:/bin", "LC_ALL=C", "HOME=/root",
                              "TERM=dumb", NULL};
static bool run(char *const arguments[], int expected, TiredText *output, TiredError *error)
{
    TiredProcess *process = NULL;
    if (!tired_process_start(arguments[0], arguments, environment, 120000, TIRED_INPUT_LIMIT,
                             &process, error))
        return false;
    while (!tired_process_step(process))
    {
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
    }
    TiredProcessResult result = tired_process_result(process);
    bool ok = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == expected;
    if (output != NULL)
        ok = tired_text_set(output, result.standard_output, result.output_length, TIRED_INPUT_LIMIT,
                            error) &&
             ok;
    if (!ok)
        fprintf(stderr, "Command %s returned outcome=%d exit=%d (expected=%d)\n%.*s\n%.*s\n",
                arguments[0], result.outcome, result.exit_code, expected, (int)result.output_length,
                result.standard_output, (int)result.error_length, result.standard_error);
    tired_process_destroy(process);
    return ok;
}
static bool command(const char *action, const char *unit, TiredText *output, TiredError *error)
{
    char *arguments[] = {frontend, (char *)action, (char *)unit, "--json", "--yes", NULL};
    return run(arguments, 0, output, error);
}
static bool state(TiredBackend *backend, const char *name, bool running, bool enabled,
                  TiredError *error)
{
    TiredRuntime runtime = {0};
    bool ok = backend->query(backend->context, name, true, &runtime, error) &&
              runtime.running == running && runtime.enabled == enabled;
    if (!ok)
        fprintf(stderr, "%s: running=%d enabled=%d state=%s sub=%s\n", name, runtime.running,
                runtime.enabled,
                runtime.active_state.data == NULL ? "unknown" : runtime.active_state.data,
                runtime.sub_state.data == NULL ? "unknown" : runtime.sub_state.data);
    tired_runtime_destroy(&runtime);
    return ok;
}
static bool field_true(struct json_object *document, const char *group, const char *field)
{
    struct json_object *object = NULL, *value = NULL;
    return json_object_object_get_ex(document, group, &object) &&
           json_object_object_get_ex(object, field, &value) && json_object_get_boolean(value);
}
int main(int argc, char **argv)
{
    struct stat marker;
    if ((argc != 2 && argc != 3 && argc != 4) || getuid() != 0 ||
        lstat("/opt/tired-tests/disposable", &marker) != 0 || !S_ISREG(marker.st_mode) ||
        marker.st_uid != 0 || (marker.st_mode & 0022) != 0)
    {
        fputs("This test requires an explicitly prepared disposable VM marker and root.\n", stderr);
        return 3;
    }
    if (argc >= 3)
    {
        if (argv[2][0] != '/')
            return 2;
        frontend = argv[2];
    }
    if (strcmp(argv[1], "reboot") == 0)
    {
        if (argc != 4 || strcmp(argv[3], "--allow-vm-reboot") != 0)
            return 3;
        TiredError error = {0};
        char *detect[] = {"/usr/bin/systemd-detect-virt", "--vm", NULL};
        char *reboot[] = {"/usr/bin/systemctl", "reboot", NULL};
        return run(detect, 0, NULL, &error) && run(reboot, 0, NULL, &error) ? 0 : 1;
    }
    if (argc == 4)
        return 3;
    if (chdir("/opt/tired-tests") != 0)
        return 1;
    int result = 1;
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredText output = {0}, report = {0};
    TiredError error = {0};
    struct json_object *document = NULL;
    CHECK(tired_layout_discover(false, &layout, &error));
    CHECK(tired_backend_open(&layout, &native, &backend, &error));
    if (strcmp(argv[1], "reset") == 0)
    {
        const char *units[] = {"tired-qualification-live", "tired-qualification-argv",
                               "tired-qualification-renamed", "tired-qualification-failed"};
        for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); ++i)
        {
            char path[256];
            snprintf(path, sizeof(path), "/etc/systemd/system/%s.service", units[i]);
            if (access(path, F_OK) == 0)
                CHECK(command("remove", units[i], &output, &error));
        }
        const char *owned_outputs[] = {"/opt/tired-tests/argv.report",
                                       "/opt/tired-tests/external.env",
                                       "/opt/tired-tests/discovery-marker"};
        for (size_t i = 0; i < sizeof(owned_outputs) / sizeof(owned_outputs[0]); ++i)
        {
            struct stat file;
            if (lstat(owned_outputs[i], &file) != 0)
            {
                CHECK(errno == ENOENT);
                continue;
            }
            CHECK(S_ISREG(file.st_mode) && file.st_uid == 0 && file.st_nlink == 1);
            CHECK(unlink(owned_outputs[i]) == 0);
        }
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "after-reboot") == 0)
    {
        CHECK(state(&backend, "tired-qualification-boot.service", true, true, &error));
        puts("PASS: committed service survived actual VM reboot.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "cleanup") == 0)
    {
        CHECK(command("remove", "tired-qualification-boot", &output, &error));
        puts("PASS: removed boot fixture without removing application payload.");
        result = 0;
        goto cleanup;
    }
    CHECK(strcmp(argv[1], "first") == 0);
    char *plan[] = {frontend,    "plan",
                    "--offline", "--profile",
                    "none",      "--json",
                    "--",        "/opt/tired-tests/tired_fixture",
                    "marker",    "/opt/tired-tests/discovery-marker",
                    NULL};
    CHECK(run(plan, 0, &output, &error));
    CHECK(access("/opt/tired-tests/discovery-marker", F_OK) != 0 && errno == ENOENT);
    puts("PASS: offline discovery did not execute workload.");
    CHECK(tired_write_private_new("/opt/tired-tests/external.env", "TIRED_TEST_ENV=external\n", 24,
                                  &error));
    char *roundtrip[] = {frontend,
                         "create",
                         "--name",
                         "tired-qualification-argv",
                         "--profile",
                         "none",
                         "--type",
                         "oneshot",
                         "--restart",
                         "no",
                         "--no-enable",
                         "--yes",
                         "--no-tui",
                         "--json",
                         "--env-file",
                         "/opt/tired-tests/external.env",
                         "--env",
                         "TIRED_TEST_ENV=managed",
                         "--env",
                         "TIRED_TEST_TOKEN=fixture-private-value",
                         "--",
                         "/opt/tired-tests/tired_fixture",
                         "report",
                         "/opt/tired-tests/argv.report",
                         "",
                         "with spaces",
                         "literal'quote\"",
                         "$HOME",
                         "${TOKEN}",
                         "%n",
                         "\\",
                         ";",
                         "\t",
                         "\n",
                         "--name",
                         "--help",
                         NULL};
    CHECK(run(roundtrip, 0, &output, &error));
    CHECK(strstr(output.data, "fixture-private-value") == NULL);
    CHECK(tired_read_file("/opt/tired-tests/argv.report", TIRED_INPUT_LIMIT, &report, &error));
    const char *expected[] = {"arg 0 \n",
                              "arg 11 7769746820737061636573\n",
                              "arg 14 6c69746572616c2771756f746522\n",
                              "arg 5 24484f4d45\n",
                              "arg 8 247b544f4b454e7d\n",
                              "arg 2 256e\n",
                              "arg 1 5c\n",
                              "arg 1 3b\n",
                              "arg 1 09\n",
                              "arg 1 0a\n",
                              "arg 6 2d2d6e616d65\n",
                              "arg 6 2d2d68656c70\n",
                              "environment 7 6d616e61676564\n"};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        CHECK(strstr(report.data, expected[i]) != NULL);
    puts("PASS: real systemd argv fidelity and managed/live environment precedence.");
    CHECK(command("remove", "tired-qualification-argv", &output, &error));
    CHECK(access("/opt/tired-tests/argv.report", F_OK) == 0 &&
          access("/opt/tired-tests/external.env", F_OK) == 0);
    char *create[] = {frontend,
                      "create",
                      "--name",
                      "tired-qualification-live",
                      "--profile",
                      "none",
                      "--restart",
                      "always",
                      "--yes",
                      "--no-tui",
                      "--json",
                      "--",
                      "/opt/tired-tests/tired_fixture",
                      "loop",
                      NULL};
    CHECK(run(create, 0, &output, &error));
    CHECK(state(&backend, "tired-qualification-live.service", true, true, &error));
    CHECK(command("disable", "tired-qualification-live", &output, &error));
    CHECK(state(&backend, "tired-qualification-live.service", true, false, &error));
    CHECK(command("enable", "tired-qualification-live", &output, &error));
    CHECK(command("stop", "tired-qualification-live", &output, &error));
    struct timespec pause = {.tv_sec = 1};
    nanosleep(&pause, NULL);
    CHECK(state(&backend, "tired-qualification-live.service", false, true, &error));
    CHECK(command("start", "tired-qualification-live", &output, &error));
    CHECK(command("restart", "tired-qualification-live", &output, &error));
    puts("PASS: native start/stop/restart and independent enable/disable semantics.");
    char *edit[] = {frontend,
                    "edit",
                    "tired-qualification-live",
                    "--apply-mode",
                    "defer",
                    "--description",
                    "Deferred fixture",
                    "--yes",
                    "--json",
                    NULL};
    CHECK(run(edit, 0, &output, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(field_true(document, "runtime", "earlier_start_context"));
    json_object_put(document);
    document = NULL;
    CHECK(state(&backend, "tired-qualification-live.service", true, true, &error));
    char *rename[] = {
        frontend, "rename", "tired-qualification-live", "tired-qualification-renamed", "--yes",
        "--json", NULL};
    CHECK(run(rename, 0, &output, &error));
    CHECK(state(&backend, "tired-qualification-renamed.service", true, true, &error));
    CHECK(command("remove", "tired-qualification-renamed", &output, &error));
    puts("PASS: deferred edit, rename and owned-only removal.");
    char *failed[] = {
        frontend, "create", "--name", "tired-qualification-failed",     "--profile", "none",
        "--yes",  "--json", "--",     "/opt/tired-tests/tired_fixture", "exit",      "1",
        NULL};
    CHECK(run(failed, 7, &output, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    struct json_object *installation = NULL, *value = NULL;
    CHECK(json_object_object_get_ex(document, "installation", &installation));
    CHECK(json_object_object_get_ex(installation, "state", &value));
    CHECK(strcmp(json_object_get_string(value), "committed") == 0);
    json_object_put(document);
    document = NULL;
    CHECK(command("remove", "tired-qualification-failed", &output, &error));
    puts("PASS: failed startup retained committed installation and returned code 7.");
    char *boot[] = {frontend,    "create",
                    "--name",    "tired-qualification-boot",
                    "--profile", "none",
                    "--yes",     "--json",
                    "--env",     "TIRED_TEST_ENV=survives-package-removal",
                    "--",        "/opt/tired-tests/tired_fixture",
                    "loop",      NULL};
    CHECK(run(boot, 0, &output, &error));
    CHECK(state(&backend, "tired-qualification-boot.service", true, true, &error));
    puts("PASS: enabled boot fixture prepared for actual reboot/package lifecycle qualification.");
    result = 0;
cleanup:
    json_object_put(document);
    tired_backend_destroy(native);
    tired_layout_destroy(&layout);
    tired_text_destroy(&output);
    tired_text_destroy(&report);
    if (result != 0)
        fputs("FAILED: inspect only tired-qualification-* units in this disposable VM.\n", stderr);
    return result;
}
