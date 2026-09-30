#define _GNU_SOURCE
#include "tired/backend.h"
#include "tired/io.h"
#include "tired/process.h"
#include "tired/service_inventory.h"
#include "tired/service_record_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CHECK(e)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(e))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "Package qualification failed at %d: %s (%s)\n", __LINE__, #e,         \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static char *environment[] = {"PATH=/usr/local/bin:/usr/bin:/bin", "LC_ALL=C", "HOME=/root",
                              "DEBIAN_FRONTEND=noninteractive", NULL};
static bool run(char *const arguments[], TiredError *error)
{
    TiredProcess *process = NULL;
    if (!tired_process_start(arguments[0], arguments, environment, 300000, TIRED_INPUT_LIMIT,
                             &process, error))
        return false;
    while (!tired_process_step(process))
    {
        struct timespec delay = {.tv_nsec = 20000000};
        nanosleep(&delay, NULL);
    }
    TiredProcessResult result = tired_process_result(process);
    bool ok = result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 0;
    if (!ok)
        fprintf(stderr, "%s: exit=%d\n%.*s\n%.*s\n", arguments[0], result.exit_code,
                (int)result.output_length, result.standard_output, (int)result.error_length,
                result.standard_error);
    tired_process_destroy(process);
    return ok;
}
int main(int argc, char **argv)
{
    struct stat marker;
    if ((argc != 3 && !(argc == 4 && strcmp(argv[1], "deb") == 0)) || getuid() != 0 ||
        lstat("/opt/tired-tests/disposable", &marker) != 0 || !S_ISREG(marker.st_mode) ||
        marker.st_uid != 0 || (marker.st_mode & 0022) != 0)
        return 3;
    int result = 1;
    TiredError error = {0};
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredRuntime runtime = {0};
    TiredText bytes = {0};
    TiredServiceRecord record = {0};
    TiredServiceInventory inventory = {0};
    const TiredServiceMetadata *metadata = NULL;
    CHECK(tired_layout_discover(false, &layout, &error));
    CHECK(tired_backend_open(&layout, &native, &backend, &error));
    if (strcmp(argv[1], "strict") == 0)
    {
        CHECK(argv[2][0] == '/');
        char *install[] = {"/usr/bin/snap", "install", "--dangerous", argv[2], NULL};
        char *observe[] = {"/usr/bin/snap", "connect", "tired:system-observe", NULL};
        char *journal[] = {"/usr/bin/snap", "connect", "tired:log-observe", NULL};
        CHECK(run(install, &error) && run(observe, &error) && run(journal, &error));
        char *create[] = {"/snap/bin/tired",
                          "create",
                          "--name",
                          "tired-qualification-strict",
                          "--profile",
                          "none",
                          "--yes",
                          "--json",
                          "--",
                          "/opt/tired-tests/tired_fixture",
                          "loop",
                          NULL};
        TiredProcess *process = NULL;
        CHECK(tired_process_start(create[0], create, environment, 120000, TIRED_INPUT_LIMIT,
                                  &process, &error));
        while (!tired_process_step(process))
        {
            struct timespec delay = {.tv_nsec = 20000000};
            nanosleep(&delay, NULL);
        }
        TiredProcessResult observed = tired_process_result(process);
        bool refused = observed.outcome == TIRED_PROCESS_EXITED && observed.exit_code > 0;
        printf("Strict creation exit=%d\n%.*s\n%.*s\n", observed.exit_code,
               (int)observed.output_length, observed.standard_output, (int)observed.error_length,
               observed.standard_error);
        tired_process_destroy(process);
        CHECK(refused &&
              access("/etc/systemd/system/tired-qualification-strict.service", F_OK) != 0);
        char *remove[] = {"/usr/bin/snap", "remove", "tired", "--purge", NULL};
        CHECK(run(remove, &error));
        puts("PASS: strict confinement with observation interfaces cannot perform host creation.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "deb") == 0)
    {
        CHECK(argv[2][0] == '/');
        char *install[] = {"/usr/bin/apt-get", "install", "--yes", "--reinstall", argv[2], NULL};
        CHECK(run(install, &error));
        char *test[] = {"/opt/tired-tests/tired_native_test", "reset", "/usr/bin/tired", NULL};
        char *first[] = {"/opt/tired-tests/tired_native_test", "first", "/usr/bin/tired", NULL};
        if (access("/etc/systemd/system/tired-qualification-boot.service", F_OK) == 0)
        {
            char *old[] = {"/usr/bin/tired", "remove", "tired-qualification-boot",
                           "--yes",          "--json", NULL};
            CHECK(run(old, &error));
        }
        CHECK(run(test, &error) && run(first, &error));
        CHECK(tired_service_inventory_load(&layout, &inventory, &error));
        CHECK(tired_service_inventory_find(&inventory, "tired-qualification-boot.service",
                                           &metadata, &error));
        CHECK(tired_service_record_load(&layout, metadata->service_uuid, &record, &error));
        CHECK(record.has_environment && access(record.environment_path.data, R_OK) == 0);
        char *upgrade[] = {"/usr/bin/apt-get", "install", "--yes", "--reinstall", argv[2], NULL};
        CHECK(run(upgrade, &error));
        if (argc == 4)
        {
            CHECK(argv[3][0] == '/');
            char *new_version[] = {"/usr/bin/apt-get", "install", "--yes", argv[3], NULL};
            CHECK(run(new_version, &error));
            puts("PASS: package version upgrade retained existing service identity and state.");
        }
        CHECK(backend.query(backend.context, "tired-qualification-boot.service", true, &runtime,
                            &error));
        CHECK(runtime.running && runtime.enabled);
        char *remove[] = {"/usr/bin/apt-get", "remove", "--yes", "tired", NULL};
        char *purge[] = {"/usr/bin/apt-get", "purge", "--yes", "tired", NULL};
        CHECK(run(remove, &error));
        CHECK(access(record.unit_path.data, R_OK) == 0 &&
              access(record.environment_path.data, R_OK) == 0);
        char *restore[] = {"/usr/bin/apt-get", "install", "--yes", argc == 4 ? argv[3] : argv[2],
                           NULL};
        CHECK(run(restore, &error) && run(purge, &error));
        CHECK(access(record.unit_path.data, R_OK) == 0 &&
              access(record.environment_path.data, R_OK) == 0);
        CHECK(access("/usr/bin/tired", F_OK) != 0);
        puts("PASS: Debian install/reinstall/removal/purge preserved generated unit, private "
             "environment and app.");
    }
    else if (strcmp(argv[1], "snap") == 0)
    {
        CHECK(argv[2][0] == '/');
        char *install[] = {"/usr/bin/snap", "install", "--dangerous", "--classic", argv[2], NULL};
        CHECK(run(install, &error));
        char *test[] = {"/opt/tired-tests/tired_native_test", "reset", "/snap/bin/tired", NULL};
        char *first[] = {"/opt/tired-tests/tired_native_test", "first", "/snap/bin/tired", NULL};
        if (access("/etc/systemd/system/tired-qualification-boot.service", F_OK) == 0)
        {
            char *old[] = {"/snap/bin/tired", "remove", "tired-qualification-boot",
                           "--yes",           "--json", NULL};
            CHECK(run(old, &error));
        }
        CHECK(run(test, &error) && run(first, &error));
        CHECK(tired_service_inventory_load(&layout, &inventory, &error));
        CHECK(tired_service_inventory_find(&inventory, "tired-qualification-boot.service",
                                           &metadata, &error));
        CHECK(tired_service_record_load(&layout, metadata->service_uuid, &record, &error));
        CHECK(tired_read_file(record.unit_path.data, TIRED_INPUT_LIMIT, &bytes, &error));
        CHECK(strstr(bytes.data, "/snap/") == NULL && strstr(bytes.data, "/var/snap/") == NULL);
        char *refresh[] = {"/usr/bin/snap", "install", "--dangerous", "--classic", argv[2], NULL};
        CHECK(run(refresh, &error));
        char *remove[] = {"/usr/bin/snap", "remove", "tired", "--purge", NULL};
        CHECK(run(remove, &error));
        CHECK(access(record.unit_path.data, R_OK) == 0 &&
              access(record.environment_path.data, R_OK) == 0);
        puts("PASS: classic Snap host lifecycle, local revision replacement and removal preserved "
             "host state.");
    }
    else
        CHECK(strcmp(argv[1], "check") == 0);
    bool uncertain = false;
    CHECK(backend.job(backend.context, "tired-qualification-boot.service", TIRED_JOB_RESTART,
                      &uncertain, &error));
    CHECK(!uncertain && backend.query(backend.context, "tired-qualification-boot.service", true,
                                      &runtime, &error));
    CHECK(runtime.running && runtime.enabled && runtime.pid > 0);
    char process_environment[96];
    snprintf(process_environment, sizeof(process_environment), "/proc/%llu/environ",
             (unsigned long long)runtime.pid);
    CHECK(tired_read_file(process_environment, TIRED_INPUT_LIMIT, &bytes, &error));
    const char *assignment = "TIRED_TEST_ENV=survives-package-removal";
    CHECK(memmem(bytes.data, bytes.length, assignment, strlen(assignment)) != NULL);
    puts("PASS: native restart after package removal retained the exact managed environment.");
    result = 0;
cleanup:
    tired_service_record_destroy(&record);
    tired_service_inventory_destroy(&inventory);
    tired_runtime_destroy(&runtime);
    tired_backend_destroy(native);
    tired_layout_destroy(&layout);
    tired_text_destroy(&bytes);
    return result;
}
