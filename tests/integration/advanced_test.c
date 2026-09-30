#define _GNU_SOURCE
#include "tired/backend.h"
#include "tired/encode.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/mutation.h"
#include "tired/process.h"
#include "tired/render.h"
#include "tired/service_files.h"
#include "tired/service_record_storage.h"
#include "tired/transaction_inventory.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define REQUIRE(condition)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "Qualification failed at %d: %s (%s)\n", __LINE__, #condition,         \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static char *frontend, *self;
static bool user_command;
static char *environment[] = {"PATH=/usr/local/bin:/usr/bin:/bin", "LC_ALL=C", "TERM=dumb",
                              "HOME=/root", NULL};
static bool run(char *const argv[], int expected, TiredText *output, TiredError *error)
{
    char *wrapped[128];
    char *const *arguments = argv;
    char *program = argv[0];
    if (user_command)
    {
        wrapped[0] = self;
        wrapped[1] = "--user-command";
        size_t i = 0;
        for (; argv[i] != NULL && i < 124; ++i)
            wrapped[i + 2] = argv[i];
        wrapped[i + 2] = NULL;
        arguments = wrapped;
        program = self;
    }
    TiredProcess *process = NULL;
    if (!tired_process_start(program, arguments, environment, 120000, TIRED_INPUT_LIMIT, &process,
                             error))
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
        fprintf(stderr, "%s: exit=%d expected=%d\n%.*s\n%.*s\n", program, result.exit_code,
                expected, (int)result.output_length, result.standard_output,
                (int)result.error_length, result.standard_error);
    tired_process_destroy(process);
    return ok;
}
static bool lifecycle(const char *action, const char *name, int expected, TiredText *output,
                      TiredError *error)
{
    char *argv[] = {frontend, (char *)action, (char *)name,
                    "--yes",  "--json",       user_command ? "--user" : "--system",
                    NULL};
    return run(argv, expected, output, error);
}
static bool wait_running(TiredBackend *backend, const char *name, uint64_t previous_pid,
                         uint64_t minimum_restarts, unsigned seconds, TiredRuntime *runtime,
                         TiredError *error)
{
    uint64_t deadline = tired_monotonic_usec() + (uint64_t)seconds * 1000000;
    do
    {
        if (!backend->query(backend->context, name, true, runtime, error))
            return false;
        if (runtime->running && (previous_pid == 0 || runtime->pid != previous_pid) &&
            (minimum_restarts == 0 ||
             (runtime->restarts_known && runtime->restarts >= minimum_restarts)))
            return true;
        struct timespec pause = {.tv_nsec = 100000000};
        nanosleep(&pause, NULL);
    } while (tired_monotonic_usec() < deadline);
    return false;
}
static bool owned_unlink(const char *path)
{
    struct stat file;
    if (lstat(path, &file) != 0)
        return errno == ENOENT;
    return S_ISREG(file.st_mode) && file.st_uid == 0 && file.st_nlink == 1 && unlink(path) == 0;
}
typedef struct
{
    const char *phase;
    TiredEffectSink sink;
    void *sink_context;
    void (*native_effects)(void *, TiredEffectSink, void *);
} RecoveryCrash;
static RecoveryCrash recovery_crash;
static void recovery_tick(void *context, const char *phase)
{
    (void)context;
    if (strcmp(recovery_crash.phase, phase) == 0)
    {
        (void)raise(SIGKILL);
        _exit(98);
    }
}
static bool recovery_effect(void *context, const char *kind, const TiredText *bytes,
                            TiredError *error)
{
    RecoveryCrash *crash = context;
    bool ok = crash->sink(crash->sink_context, kind, bytes, error);
    if (ok)
        recovery_tick(NULL, kind);
    return ok;
}
static void recovery_effects(void *context, TiredEffectSink sink, void *sink_context)
{
    recovery_crash.sink = sink;
    recovery_crash.sink_context = sink_context;
    recovery_crash.native_effects(context, sink == NULL ? NULL : recovery_effect, &recovery_crash);
}
/* Private state is isolated from the ordinary installed inventory. Units and
 * jobs use the real manager, and only these exact qualification units are changed.
 * Uncertain records remain available in the disposable guest for inspection. */
static bool native_recovery_case(const TiredLayout *production, unsigned index)
{
    int result = 1;
    TiredError error = {0};
    TiredRequest request = {0};
    TiredPlan plan = {0};
    TiredSettings settings = {0};
    TiredMutation mutation = {0};
    TiredOperationResult outcome = {0};
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredRuntime runtime = {0};
    TiredText root = {0}, bytes = {0};
    TiredTextList risks = {0};
    TiredDirectory *records = NULL;
    TiredFileFingerprint fingerprint = {0};
    char fixture[] = "/opt/tired-tests/recovery-XXXXXX", name[128], record_name[42];
    REQUIRE(mkdtemp(fixture) != NULL);
    REQUIRE(tired_text_set(&root, fixture, strlen(fixture), 4096, &error));
    for (size_t i = 0; i < TIRED_PATH_COUNT; ++i)
        REQUIRE(tired_text_set(&layout.paths[i], production->paths[i].data,
                               production->paths[i].length, 4096, &error));
    const TiredLayoutPath roles[] = {TIRED_PATH_RECORDS, TIRED_PATH_HISTORY,
                                     TIRED_PATH_TRANSACTIONS, TIRED_PATH_OPERATION_LOCK};
    const char *leaves[] = {"records", "history", "transactions", "run/operation.lock"};
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
        REQUIRE(tired_path_absolute(&root, leaves[i], strlen(leaves[i]), &layout.paths[roles[i]],
                                    &error));
    (void)snprintf(name, sizeof(name), "tired-qualification-recovery-%u", index);
    const char *arguments[] = {
        "tired",    "--name", name,          "--profile", "none",
        "--run-as", "root",   "--no-enable", "--",        "/opt/tired-tests/tired_fixture",
        "loop"};
    REQUIRE(tired_cli_parse((int)(sizeof(arguments) / sizeof(arguments[0])), arguments, &request,
                            &error));
    REQUIRE(tired_plan_prepare(&request, &plan, &error));
    tired_settings_defaults(&settings);
    settings.observation_usec = 100000;
    REQUIRE(tired_text_list_append(&risks, "run-as-root", 11, 16, 4096, &error));
    REQUIRE(tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, &error));
    REQUIRE(tired_backend_open(&layout, &native, &backend, &error));
    REQUIRE(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    REQUIRE(outcome.status == TIRED_OK && outcome.runtime.running && outcome.runtime.pid > 0);
    uint64_t old_pid = outcome.runtime.pid;
    mutation.operation =
        index == 1 || index == 3 || index == 4 ? TIRED_TRANSACTION_EDIT : TIRED_TRANSACTION_RESTART;
    REQUIRE(tired_text_set(&mutation.expected_unit_sha256, mutation.proposed.metadata.unit_sha256,
                           64, 64, &error));
    (void)snprintf(record_name, sizeof(record_name), "%s.json",
                   mutation.proposed.metadata.service_uuid);
    REQUIRE(tired_directory_open(layout.paths[TIRED_PATH_RECORDS].data, 0, true, &records, &error));
    REQUIRE(tired_file_fingerprint(records, record_name, TIRED_SERVICE_RECORD_LIMIT, &fingerprint,
                                   &error));
    REQUIRE(tired_text_set(&mutation.expected_record_sha256, fingerprint.sha256, 64, 64, &error));
    REQUIRE(tired_uuid_create(mutation.proposed.metadata.transaction_uuid, &error));
    if (mutation.operation == TIRED_TRANSACTION_EDIT)
    {
        REQUIRE(tired_uuid_create(mutation.proposed.metadata.revision_uuid, &error));
        REQUIRE(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_DESCRIPTION,
                               "Native recovery revision", 24, TIRED_ORIGIN_USER, true, &error));
        if (index == 4)
            REQUIRE(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_START, "false", 5,
                                   TIRED_ORIGIN_USER, true, &error));
        REQUIRE(tired_render_unit(&mutation.proposed.spec, mutation.proposed.metadata.service_uuid,
                                  NULL, &mutation.proposed.credentials, &bytes, &error));
        REQUIRE(tired_digest_bytes(bytes.data, bytes.length, mutation.proposed.metadata.unit_sha256,
                                   &error));
    }
    REQUIRE(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0)
    {
        TiredNativeBackend *child_native = NULL;
        TiredBackend child_backend = {0};
        if (!tired_backend_open(&layout, &child_native, &child_backend, &error))
            _exit(97);
        recovery_crash.phase = index == 4 ? "prepared" : index < 2 ? "restart" : "job_finished";
        recovery_crash.native_effects = child_backend.effects;
        child_backend.effects = recovery_effects;
        child_backend.tick = recovery_tick;
        (void)tired_mutation_apply(&mutation, &layout, &child_backend, &outcome, &error);
        _exit(98);
    }
    int status;
    REQUIRE(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
            WTERMSIG(status) == SIGKILL);
    REQUIRE(tired_mutation_recover(&layout, &backend, mutation.proposed.metadata.transaction_uuid,
                                   true, &outcome, &error));
    REQUIRE(backend.query(backend.context, mutation.proposed.metadata.unit_name.data, true,
                          &runtime, &error));
    if (index < 2)
        REQUIRE(outcome.status == TIRED_RECOVERY_REQUIRED && outcome.recovery_required &&
                runtime.running && runtime.pid == old_pid);
    else if (index == 4)
        REQUIRE(outcome.status == TIRED_OK && !runtime.active && !runtime.running &&
                runtime.pid == 0 && runtime.job_known && runtime.job_id == 0);
    else
        REQUIRE(outcome.status == TIRED_OK && runtime.running && runtime.pid != old_pid);
    TiredServiceFiles files = {0};
    REQUIRE(tired_service_files_inspect(&layout, &mutation.proposed, &files, &error) &&
            files.unit.state == TIRED_SERVICE_FILE_MATCH);
    bool uncertain = false;
    REQUIRE(backend.job(backend.context, mutation.proposed.metadata.unit_name.data, TIRED_JOB_STOP,
                        &uncertain, &error) &&
            !uncertain);
    REQUIRE(backend.enable(backend.context, mutation.proposed.metadata.unit_name.data, false,
                           &uncertain, &error) &&
            !uncertain);
    REQUIRE(owned_unlink(mutation.proposed.unit_path.data));
    REQUIRE(backend.reload(backend.context, &uncertain, &error) && !uncertain);
    result = 0;
cleanup:
    tired_runtime_destroy(&runtime);
    tired_backend_destroy(native);
    tired_directory_destroy(records);
    tired_mutation_destroy(&mutation);
    tired_operation_result_destroy(&outcome);
    tired_settings_destroy(&settings);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    tired_layout_destroy(&layout);
    tired_text_list_destroy(&risks);
    tired_text_destroy(&root);
    tired_text_destroy(&bytes);
    return result == 0;
}
int main(int argc, char **argv)
{
    struct stat marker;
    if (getuid() != 0 || lstat("/opt/tired-tests/disposable", &marker) != 0 ||
        !S_ISREG(marker.st_mode) || marker.st_uid != 0 || (marker.st_mode & 0022) != 0)
        return 3;
    if (argc > 3 && strcmp(argv[1], "--user-command") == 0)
    {
        struct passwd *account = getpwnam("tired-qualification");
        if (account == NULL || account->pw_uid != 1501 ||
            initgroups(account->pw_name, account->pw_gid) != 0 || setgid(account->pw_gid) != 0 ||
            setuid(account->pw_uid) != 0)
            return 3;
        char *user_environment[] = {"PATH=/usr/local/bin:/usr/bin:/bin",
                                    "LC_ALL=C",
                                    "TERM=dumb",
                                    "HOME=/home/tired-qualification",
                                    "XDG_RUNTIME_DIR=/run/user/1501",
                                    NULL};
        execve(argv[2], &argv[2], user_environment);
        return 127;
    }
    if (argc != 3 || argv[2][0] != '/')
        return 2;
    frontend = argv[2];
    self = argv[0];
    int result = 1;
    TiredError error = {0};
    TiredText output = {0}, report = {0};
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredRuntime runtime = {0};
    TiredProcess *account_session = NULL;
    bool policy_created = false;
    REQUIRE(chdir("/opt/tired-tests") == 0);
    REQUIRE(tired_layout_discover(false, &layout, &error));
    REQUIRE(tired_backend_open(&layout, &native, &backend, &error));
    if (strcmp(argv[1], "recovery") == 0)
    {
        for (unsigned i = 0; i < 5; ++i)
            REQUIRE(native_recovery_case(&layout, i));
        puts(
            "PASS: native restart/edit intent preserves the old PID and remains unresolved; "
            "durable job completion proves replacement; interrupted no-start edit stops the unit.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "user-after-reboot") == 0)
    {
        user_command = true;
        char *status[] = {frontend, "status", "tired-qualification-user", "--user", "--json", NULL};
        REQUIRE(run(status, 0, &output, &error));
        REQUIRE(strstr(output.data, "\"running\":true") != NULL ||
                strstr(output.data, "\"SubState\":\"running\"") != NULL);
        puts("PASS: lingering user service survived actual boot without an account login.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "user") == 0)
    {
        struct passwd *account = getpwnam("tired-qualification");
        if (account == NULL)
        {
            char *add[] = {"/usr/sbin/useradd",   "--uid",   "1501",
                           "--create-home",       "--shell", "/bin/bash",
                           "tired-qualification", NULL};
            REQUIRE(run(add, 0, NULL, &error));
            account = getpwnam("tired-qualification");
        }
        REQUIRE(account != NULL && account->pw_uid == 1501);
        const char *login_key = "/opt/tired-tests/user-login-key";
        struct stat existing_key;
        if (lstat(login_key, &existing_key) != 0)
        {
            REQUIRE(errno == ENOENT);
            char *generate[] = {"/usr/bin/ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f",
                                (char *)login_key,     NULL};
            REQUIRE(run(generate, 0, NULL, &error));
        }
        REQUIRE(lstat(login_key, &existing_key) == 0 && S_ISREG(existing_key.st_mode) &&
                existing_key.st_uid == 0 && existing_key.st_nlink == 1 &&
                (existing_key.st_mode & 0077) == 0);
        char *ssh_directory[] = {"/usr/bin/install",
                                 "-d",
                                 "-m",
                                 "0700",
                                 "-o",
                                 "1501",
                                 "-g",
                                 "1501",
                                 "/home/tired-qualification/.ssh",
                                 NULL};
        char *authorize[] = {"/usr/bin/install",
                             "-m",
                             "0600",
                             "-o",
                             "1501",
                             "-g",
                             "1501",
                             "/opt/tired-tests/user-login-key.pub",
                             "/home/tired-qualification/.ssh/authorized_keys",
                             NULL};
        REQUIRE(run(ssh_directory, 0, NULL, &error) && run(authorize, 0, NULL, &error));
        char *session[] = {"/usr/bin/ssh",
                           "-i",
                           (char *)login_key,
                           "-o",
                           "BatchMode=yes",
                           "-o",
                           "StrictHostKeyChecking=no",
                           "-o",
                           "UserKnownHostsFile=/dev/null",
                           "-o",
                           "LogLevel=ERROR",
                           "tired-qualification@127.0.0.1",
                           "/opt/tired-tests/tired_fixture",
                           "loop",
                           NULL};
        REQUIRE(tired_process_start(session[0], session, environment, 300000, TIRED_INPUT_LIMIT,
                                    &account_session, &error));
        bool session_ready = false;
        uint64_t session_deadline = tired_monotonic_usec() + 60000000;
        while (tired_monotonic_usec() < session_deadline)
        {
            bool ended = tired_process_step(account_session);
            TiredProcessResult login = tired_process_result(account_session);
            static const char ready[] = "fixture running uid=1501 ";
            if (login.output_length >= sizeof(ready) - 1 &&
                memmem(login.standard_output, login.output_length, ready, sizeof(ready) - 1) !=
                    NULL)
            {
                session_ready = !ended;
                break;
            }
            REQUIRE(!ended);
            struct timespec pause = {.tv_nsec = 20000000};
            nanosleep(&pause, NULL);
        }
        REQUIRE(session_ready);
        char *manager[] = {"/usr/bin/systemctl", "start", "user@1501.service", NULL};
        REQUIRE(run(manager, 0, NULL, &error));
        char *clear_linger[] = {"/usr/bin/loginctl", "disable-linger", "tired-qualification", NULL};
        REQUIRE(run(clear_linger, 0, NULL, &error));
        user_command = true;
        if (access(
                "/home/tired-qualification/.config/systemd/user/tired-qualification-user.service",
                F_OK) == 0)
            REQUIRE(lifecycle("remove", "tired-qualification-user", 0, &output, &error));
        char *no_approval[] = {
            frontend,    "create", "--system", "--name", "tired-qualification-auth",
            "--profile", "none",   "--json",   "--",     "/opt/tired-tests/tired_fixture",
            "loop",      NULL};
        REQUIRE(run(no_approval, 2, &output, &error));
        char *create[] = {
            frontend, "create", "--system", "--name", "tired-qualification-auth",       "--profile",
            "none",   "--yes",  "--json",   "--",     "/opt/tired-tests/tired_fixture", "loop",
            NULL};
        REQUIRE(run(create, 4, &output, &error));
        REQUIRE(access("/etc/systemd/system/tired-qualification-auth.service", F_OK) != 0);
        char helper[4096];
        char *slash = strrchr(frontend, '/');
        REQUIRE(slash != NULL && (size_t)(slash - frontend) + 40 < sizeof(helper));
        snprintf(helper, sizeof(helper), "%.*s/../libexec/tired/tired-helper",
                 (int)(slash - frontend), frontend);
        if (strcmp(frontend, "/snap/bin/tired") == 0)
            snprintf(helper, sizeof(helper),
                     "/snap/tired/current/usr/local/libexec/tired/tired-helper");
        char *canonical = realpath(helper, NULL);
        REQUIRE(canonical != NULL);
        TiredBuffer policy;
        tired_buffer_init(&policy, 8192);
        bool made =
            tired_buffer_append(&policy, "tired-qualification ALL=(root) NOPASSWD: ",
                                strlen("tired-qualification ALL=(root) NOPASSWD: "), &error) &&
            tired_buffer_append(&policy, canonical, strlen(canonical), &error) &&
            tired_buffer_append(&policy, "\n", 1, &error) &&
            tired_write_private_new("/etc/sudoers.d/tired-qualification", policy.data,
                                    policy.length, &error) &&
            chmod("/etc/sudoers.d/tired-qualification", 0440) == 0;
        free(canonical);
        tired_buffer_destroy(&policy);
        policy_created = made;
        REQUIRE(made);
        REQUIRE(run(create, 0, &output, &error));
        REQUIRE(strstr(output.data, "\"run_as\":\"tired-qualification\"") != NULL);
        REQUIRE(backend.query(backend.context, "tired-qualification-auth.service", true, &runtime,
                              &error));
        REQUIRE(runtime.pid > 0);
        char proc[96];
        snprintf(proc, sizeof(proc), "/proc/%llu/status", (unsigned long long)runtime.pid);
        REQUIRE(tired_read_file(proc, 65536, &report, &error));
        REQUIRE(strstr(report.data, "\nUid:\t1501\t1501\t1501\t1501\n") != NULL);
        puts("PASS: denied prompt-free authorization; fixed-helper authorization retained workload "
             "UID.");
        char *remove_system[] = {
            frontend, "remove", "tired-qualification-auth", "--system", "--yes", "--json", NULL};
        REQUIRE(run(remove_system, 0, &output, &error));
        REQUIRE(access("/etc/systemd/system/tired-qualification-auth.service", F_OK) != 0);
        char *user[] = {
            frontend, "create", "--user", "--name", "tired-qualification-user",       "--profile",
            "none",   "--yes",  "--json", "--",     "/opt/tired-tests/tired_fixture", "loop",
            NULL};
        REQUIRE(run(user, 0, &output, &error));
        REQUIRE(access("/var/lib/systemd/linger/tired-qualification", F_OK) != 0);
        puts("PASS: user service without lingering made no account-level change.");
        char *limit[] = {frontend,
                         "plan",
                         "--user",
                         "--profile",
                         "none",
                         "--nofile",
                         "infinity:infinity",
                         "--json",
                         "--",
                         "/opt/tired-tests/tired_fixture",
                         "loop",
                         NULL};
        REQUIRE(run(limit, 2, &output, &error));
        REQUIRE(strstr(output.data, "nofile-ceiling") != NULL);
        puts("PASS: user NOFILE above the observed manager ceiling was refused without host "
             "changes.");
        char *linger[] = {
            frontend, "edit", "tired-qualification-user", "--user", "--enable-linger", "--yes",
            "--json", NULL};
        REQUIRE(run(linger, 0, &output, &error));
        REQUIRE(access("/var/lib/systemd/linger/tired-qualification", F_OK) == 0);
        user_command = false;
        char *disable_linger[] = {"/usr/bin/loginctl", "disable-linger", "tired-qualification",
                                  NULL};
        REQUIRE(run(disable_linger, 0, NULL, &error));
        user_command = true;
        REQUIRE(lifecycle("stop", "tired-qualification-user", 0, &output, &error));
        REQUIRE(access("/var/lib/systemd/linger/tired-qualification", F_OK) != 0);
        REQUIRE(run(linger, 0, &output, &error));
        puts("PASS: explicit lingering enablement; later stop did not replay historical account "
             "intent.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "journal") == 0)
    {
        char *logs[] = {frontend, "logs", "tired-qualification-boot", "--json", "--lines",
                        "20",     NULL};
        REQUIRE(run(logs, 0, &output, &error));
        REQUIRE(strstr(output.data, "\"event_type\":\"journal_record\"") != NULL);
        REQUIRE(strstr(output.data, "666978747572652072756e6e696e67") != NULL);
        char *rotate[] = {"/usr/bin/journalctl", "--rotate", NULL};
        REQUIRE(run(rotate, 0, NULL, &error));
        REQUIRE(run(logs, 0, &output, &error));
        REQUIRE(strstr(output.data, "\"event_type\":\"journal_record\"") != NULL);
        REQUIRE(strstr(output.data, "666978747572652072756e6e696e67") != NULL);
        puts("PASS: populated native journal records and actual rotation retain selected-service "
             "history.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "features") == 0)
    {
        struct passwd *account = getpwnam("tired-qualification");
        if (account == NULL)
        {
            char *add[] = {"/usr/sbin/useradd",   "--uid",   "1501",
                           "--create-home",       "--shell", "/bin/bash",
                           "tired-qualification", NULL};
            REQUIRE(run(add, 0, NULL, &error));
        }
        if (getgrnam("tired-extra") == NULL)
        {
            char *add[] = {"/usr/sbin/groupadd", "--gid", "1502", "tired-extra", NULL};
            REQUIRE(run(add, 0, NULL, &error));
        }
        REQUIRE(mkdir("/opt/tired-tests/group-access", 0770) == 0 || errno == EEXIST);
        REQUIRE(chown("/opt/tired-tests/group-access", 0, 1502) == 0 &&
                chmod("/opt/tired-tests/group-access", 0770) == 0);
        struct stat old_report;
        if (lstat("/opt/tired-tests/group-access/report", &old_report) == 0)
            REQUIRE(S_ISREG(old_report.st_mode) && old_report.st_uid == 1501 &&
                    old_report.st_nlink == 1 &&
                    unlink("/opt/tired-tests/group-access/report") == 0);
        else
            REQUIRE(errno == ENOENT);
        char *group[] = {frontend,
                         "create",
                         "--name",
                         "tired-qualification-group",
                         "--profile",
                         "none",
                         "--run-as",
                         "tired-qualification",
                         "--working-directory",
                         "/opt/tired-tests/group-access",
                         "--type",
                         "oneshot",
                         "--restart",
                         "no",
                         "--set",
                         "supplementary_groups=tired-extra",
                         "--set",
                         "nice=5",
                         "--set",
                         "no_new_privileges=true",
                         "--set",
                         "private_tmp=true",
                         "--no-enable",
                         "--yes",
                         "--json",
                         "--",
                         "/opt/tired-tests/tired_fixture",
                         "report",
                         "/opt/tired-tests/group-access/report",
                         NULL};
        REQUIRE(run(group, 0, &output, &error));
        REQUIRE(tired_read_file("/opt/tired-tests/group-access/report", 65536, &report, &error));
        REQUIRE(strstr(report.data, "uid 1501\n") != NULL && strstr(report.data, " 1502") != NULL &&
                strstr(report.data, "nice 5\n") != NULL);
        REQUIRE(lifecycle("remove", "tired-qualification-group", 0, &output, &error));
        REQUIRE(unlink("/opt/tired-tests/group-access/report") == 0);
        puts("PASS: selected supplementary group, working directory, Nice and mount namespace.");
        char *remain[] = {frontend,
                          "create",
                          "--name",
                          "tired-qualification-remain",
                          "--profile",
                          "none",
                          "--restart",
                          "no",
                          "--set",
                          "remain_after_exit=true",
                          "--no-enable",
                          "--yes",
                          "--json",
                          "--",
                          "/opt/tired-tests/tired_fixture",
                          "exit",
                          "0",
                          NULL};
        REQUIRE(run(remain, 0, &output, &error));
        REQUIRE(strstr(output.data, "\"observation\":\"completed\"") != NULL);
        REQUIRE(lifecycle("remove", "tired-qualification-remain", 0, &output, &error));
        puts("PASS: RemainAfterExit completion is reported separately from a running process.");
        if (access("/etc/systemd/system/tired-qualification-disconnect.service", F_OK) == 0)
            REQUIRE(lifecycle("remove", "tired-qualification-disconnect", 0, &output, &error));
        char *disconnect[] = {frontend,
                              "create",
                              "--name",
                              "tired-qualification-disconnect",
                              "--profile",
                              "none",
                              "--type",
                              "notify",
                              "--set",
                              "timeout_start=30s",
                              "--no-enable",
                              "--yes",
                              "--json",
                              "--",
                              "/opt/tired-tests/tired_fixture",
                              "notify",
                              "5000",
                              NULL};
        pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0)
        {
            int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (fd < 0 || dup2(fd, STDIN_FILENO) < 0 || dup2(fd, STDOUT_FILENO) < 0 ||
                dup2(fd, STDERR_FILENO) < 0)
                _exit(127);
            close(fd);
            execve(frontend, disconnect, environment);
            _exit(127);
        }
        uint64_t deadline = tired_monotonic_usec() + 60000000;
        do
        {
            REQUIRE(backend.query(backend.context, "tired-qualification-disconnect.service", true,
                                  &runtime, &error));
            if (runtime.pid > 0)
                break;
            struct timespec pause = {.tv_nsec = 100000000};
            nanosleep(&pause, NULL);
        } while (tired_monotonic_usec() < deadline);
        REQUIRE(runtime.pid > 0 && kill(child, SIGKILL) == 0);
        int status;
        REQUIRE(waitpid(child, &status, 0) == child && WIFSIGNALED(status));
        REQUIRE(wait_running(&backend, "tired-qualification-disconnect.service", 0, 0, 60, &runtime,
                             &error));
        bool committed = false;
        deadline = tired_monotonic_usec() + 60000000;
        do
        {
            TiredTransactionInventory inventory = {0};
            REQUIRE(tired_transaction_inventory_load(&layout, &inventory, &error));
            committed = inventory.complete && inventory.pending_names.count == 0;
            tired_transaction_inventory_destroy(&inventory);
            if (committed)
                break;
            struct timespec pause = {.tv_nsec = 100000000};
            nanosleep(&pause, NULL);
        } while (tired_monotonic_usec() < deadline);
        REQUIRE(committed);
        REQUIRE(lifecycle("remove", "tired-qualification-disconnect", 0, &output, &error));
        puts("PASS: approved worker completed after frontend SIGKILL during notify startup.");
        result = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "adversarial") == 0)
    {
        const char *base = "tired-qualification-race";
        const char *link = "/opt/tired-tests/tired-qualification-race";
        struct stat old_link;
        if (lstat(link, &old_link) == 0)
            REQUIRE(S_ISLNK(old_link.st_mode) && old_link.st_uid == 0 && unlink(link) == 0);
        REQUIRE(symlink("/opt/tired-tests/tired_fixture", link) == 0);
        const char *old_names[] = {"tired-qualification-race", "tired-qualification-race-2"};
        for (size_t i = 0; i < sizeof(old_names) / sizeof(old_names[0]); ++i)
        {
            char old_path[256];
            snprintf(old_path, sizeof(old_path), "/etc/systemd/system/%s.service", old_names[i]);
            if (access(old_path, F_OK) == 0)
                REQUIRE(lifecycle("remove", old_names[i], 0, &output, &error));
        }
        pid_t children[2];
        char reports[2][128];
        for (unsigned i = 0; i < 2; ++i)
        {
            snprintf(reports[i], sizeof(reports[i]), "/opt/tired-tests/race-result-%u.json", i);
            REQUIRE(owned_unlink(reports[i]));
            children[i] = fork();
            REQUIRE(children[i] >= 0);
            if (children[i] == 0)
            {
                int fd = open(reports[i], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
                if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
                    _exit(127);
                close(fd);
                char *create[] = {frontend, "create", "--profile",  "none", "--no-enable", "--yes",
                                  "--json", "--",     (char *)link, "loop", NULL};
                execve(frontend, create, environment);
                _exit(127);
            }
        }
        char units[2][256];
        for (unsigned i = 0; i < 2; ++i)
        {
            int status;
            REQUIRE(waitpid(children[i], &status, 0) == children[i]);
            REQUIRE(tired_read_file(reports[i], TIRED_INPUT_LIMIT, &output, &error));
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                fprintf(stderr, "Concurrent create %u: status=%d %s\n", i, status, output.data);
            REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            struct json_object *document = NULL, *service = NULL, *unit = NULL;
            REQUIRE(
                tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
            bool valid = json_object_object_get_ex(document, "service", &service) &&
                         json_object_object_get_ex(service, "unit", &unit) &&
                         json_object_is_type(unit, json_type_string) &&
                         strlen(json_object_get_string(unit)) < sizeof(units[i]);
            if (valid)
                snprintf(units[i], sizeof(units[i]), "%s", json_object_get_string(unit));
            json_object_put(document);
            REQUIRE(valid && strncmp(units[i], base, strlen(base)) == 0);
        }
        REQUIRE(strcmp(units[0], units[1]) != 0);
        for (unsigned i = 0; i < 2; ++i)
        {
            REQUIRE(lifecycle("remove", units[i], 0, &output, &error));
            REQUIRE(owned_unlink(reports[i]));
        }
        REQUIRE(unlink(link) == 0);
        puts("PASS: concurrent automatic creates committed distinct names and independent state.");
        REQUIRE(owned_unlink("/opt/tired-tests/rename.count"));
        char *rename_fixture[] = {frontend,
                                  "create",
                                  "--name",
                                  "tired-qualification-rename-old",
                                  "--profile",
                                  "none",
                                  "--restart-sec",
                                  "1s",
                                  "--no-enable",
                                  "--yes",
                                  "--json",
                                  "--",
                                  "/opt/tired-tests/tired_fixture",
                                  "fail-times",
                                  "/opt/tired-tests/rename.count",
                                  "1",
                                  NULL};
        REQUIRE(run(rename_fixture, 7, &output, &error));
        REQUIRE(wait_running(&backend, "tired-qualification-rename-old.service", 0, 0, 30, &runtime,
                             &error));
        REQUIRE(owned_unlink("/opt/tired-tests/rename.count"));
        char *rename[] = {frontend,
                          "rename",
                          "tired-qualification-rename-old",
                          "tired-qualification-rename-new",
                          "--yes",
                          "--json",
                          NULL};
        REQUIRE(run(rename, 6, &output, &error));
        REQUIRE(strstr(output.data, "\"unit\":\"tired-qualification-rename-old.service\"") != NULL);
        REQUIRE(access("/etc/systemd/system/tired-qualification-rename-new.service", F_OK) != 0);
        REQUIRE(wait_running(&backend, "tired-qualification-rename-old.service", 0, 0, 30, &runtime,
                             &error));
        REQUIRE(lifecycle("remove", "tired-qualification-rename-old", 0, &output, &error));
        REQUIRE(owned_unlink("/opt/tired-tests/rename.count"));
        puts("PASS: failed rename restored the old identity and running process.");
        char *drift[] = {frontend,
                         "create",
                         "--name",
                         "tired-qualification-drift",
                         "--profile",
                         "none",
                         "--no-enable",
                         "--yes",
                         "--json",
                         "--",
                         "/opt/tired-tests/tired_fixture",
                         "loop",
                         NULL};
        REQUIRE(run(drift, 0, &output, &error));
        const char *fragment = "/etc/systemd/system/tired-qualification-drift.service";
        int fd = open(fragment, O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC);
        const char changed[] = "# qualification foreign edit\n";
        REQUIRE(fd >= 0 && write(fd, changed, sizeof(changed) - 1) == sizeof(changed) - 1 &&
                fsync(fd) == 0 && close(fd) == 0);
        char *edit[] = {frontend, "edit", "tired-qualification-drift", "--yes", "--json", NULL};
        REQUIRE(run(edit, 5, &output, &error));
        REQUIRE(tired_read_file(fragment, TIRED_INPUT_LIMIT, &report, &error));
        REQUIRE(strstr(report.data, changed) != NULL);
        char *restore[] = {frontend,
                           "edit",
                           "tired-qualification-drift",
                           "--restore-managed",
                           "--allow-risk",
                           "restore-drifted-unit",
                           "--yes",
                           "--json",
                           NULL};
        REQUIRE(run(restore, 0, &output, &error));
        REQUIRE(strstr(output.data, "Foreign unit bytes are backed up") != NULL);
        const char *drop_directory = "/etc/systemd/system/tired-qualification-drift.service.d";
        const char *drop_path =
            "/etc/systemd/system/tired-qualification-drift.service.d/90-qualification.conf";
        REQUIRE(mkdir(drop_directory, 0755) == 0);
        const char override[] = "[Service]\nNice=7\n";
        REQUIRE(tired_write_private_new(drop_path, override, sizeof(override) - 1, &error) &&
                chmod(drop_path, 0644) == 0);
        bool uncertain = false;
        REQUIRE(backend.reload(backend.context, &uncertain, &error) && !uncertain);
        REQUIRE(run(edit, 5, &output, &error));
        REQUIRE(owned_unlink(drop_path) && rmdir(drop_directory) == 0);
        REQUIRE(backend.reload(backend.context, &uncertain, &error));
        REQUIRE(lifecycle("remove", "tired-qualification-drift", 0, &output, &error));
        puts("PASS: foreign edit refusal, explicit backed-up restoration and drop-in conflict.");
        result = 0;
        goto cleanup;
    }
    REQUIRE(strcmp(argv[1], "system") == 0);
    const char *names[] = {"tired-qualification-retry",   "tired-qualification-limited",
                           "tired-qualification-notify",  "tired-qualification-fork",
                           "tired-qualification-timeout", "tired-qualification-idle"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        char path[256];
        snprintf(path, sizeof(path), "/etc/systemd/system/%s.service", names[i]);
        if (access(path, F_OK) == 0)
            REQUIRE(lifecycle("remove", names[i], 0, &output, &error));
    }
    REQUIRE(owned_unlink("/opt/tired-tests/retry.count"));
    char *retry[] = {frontend,
                     "create",
                     "--name",
                     "tired-qualification-retry",
                     "--profile",
                     "none",
                     "--retry-policy",
                     "persistent",
                     "--restart-sec",
                     "1s",
                     "--no-enable",
                     "--yes",
                     "--json",
                     "--",
                     "/opt/tired-tests/tired_fixture",
                     "fail-times",
                     "/opt/tired-tests/retry.count",
                     "12",
                     NULL};
    REQUIRE(run(retry, 7, &output, &error));
    REQUIRE(
        wait_running(&backend, "tired-qualification-retry.service", 0, 12, 40, &runtime, &error));
    REQUIRE(runtime.restarts_known && runtime.restarts >= 12);
    uint64_t pid = runtime.pid;
    REQUIRE(kill((pid_t)pid, SIGKILL) == 0);
    REQUIRE(
        wait_running(&backend, "tired-qualification-retry.service", pid, 12, 15, &runtime, &error));
    puts("PASS: persistent retries exceeded a finite budget; systemd restarted a killed workload.");
    char *bad_edit[] = {frontend, "edit", "tired-qualification-retry",      "--yes",
                        "--json", "--",   "/opt/tired-tests/tired_fixture", "exit",
                        "1",      NULL};
    REQUIRE(run(bad_edit, 6, &output, &error));
    REQUIRE(strstr(output.data, "\"outcome\":\"rolled_back\"") != NULL);
    REQUIRE(
        wait_running(&backend, "tired-qualification-retry.service", 0, 0, 15, &runtime, &error));
    puts("PASS: invalid active edit restored and observed the previous running revision.");
    REQUIRE(lifecycle("remove", "tired-qualification-retry", 0, &output, &error));
    REQUIRE(owned_unlink("/opt/tired-tests/retry.count"));
    REQUIRE(owned_unlink("/opt/tired-tests/limited.count"));
    char *limited[] = {frontend,
                       "create",
                       "--name",
                       "tired-qualification-limited",
                       "--profile",
                       "none",
                       "--retry-policy",
                       "limited",
                       "--restart-sec",
                       "1s",
                       "--start-limit-interval",
                       "30s",
                       "--start-limit-burst",
                       "3",
                       "--no-enable",
                       "--yes",
                       "--json",
                       "--",
                       "/opt/tired-tests/tired_fixture",
                       "fail-times",
                       "/opt/tired-tests/limited.count",
                       "100",
                       NULL};
    REQUIRE(run(limited, 7, &output, &error));
    struct timespec settle = {.tv_sec = 5};
    nanosleep(&settle, NULL);
    REQUIRE(backend.query(backend.context, "tired-qualification-limited.service", true, &runtime,
                          &error));
    REQUIRE(runtime.failed && runtime.restarts_known && runtime.restarts == 3);
    REQUIRE(tired_read_file("/opt/tired-tests/limited.count", 64, &report, &error));
    REQUIRE(strcmp(report.data, "3\n") == 0);
    REQUIRE(lifecycle("start", "tired-qualification-limited", 7, &output, &error));
    REQUIRE(backend.query(backend.context, "tired-qualification-limited.service", true, &runtime,
                          &error));
    nanosleep(&settle, NULL);
    REQUIRE(tired_read_file("/opt/tired-tests/limited.count", 64, &report, &error));
    REQUIRE(strcmp(report.data, "6\n") == 0);
    REQUIRE(lifecycle("remove", "tired-qualification-limited", 0, &output, &error));
    REQUIRE(owned_unlink("/opt/tired-tests/limited.count"));
    puts("PASS: limited retry exhaustion and explicitly scoped retry reset.");
    char *notify[] = {frontend,
                      "create",
                      "--name",
                      "tired-qualification-notify",
                      "--profile",
                      "none",
                      "--type",
                      "notify",
                      "--set",
                      "timeout_start=3s",
                      "--no-enable",
                      "--yes",
                      "--json",
                      "--",
                      "/opt/tired-tests/tired_fixture",
                      "notify",
                      "200",
                      NULL};
    REQUIRE(run(notify, 0, &output, &error));
    REQUIRE(lifecycle("remove", "tired-qualification-notify", 0, &output, &error));
    REQUIRE(owned_unlink("/opt/tired-tests/fork.pid"));
    char *forking[] = {frontend,
                       "create",
                       "--name",
                       "tired-qualification-fork",
                       "--profile",
                       "none",
                       "--type",
                       "forking",
                       "--set",
                       "pid_file=/opt/tired-tests/fork.pid",
                       "--no-enable",
                       "--yes",
                       "--json",
                       "--",
                       "/opt/tired-tests/tired_fixture",
                       "fork",
                       "/opt/tired-tests/fork.pid",
                       NULL};
    REQUIRE(run(forking, 0, &output, &error));
    REQUIRE(lifecycle("remove", "tired-qualification-fork", 0, &output, &error));
    REQUIRE(owned_unlink("/opt/tired-tests/fork.pid"));
    char *timeout[] = {frontend,
                       "create",
                       "--name",
                       "tired-qualification-timeout",
                       "--profile",
                       "none",
                       "--set",
                       "timeout_stop=1s",
                       "--no-enable",
                       "--yes",
                       "--json",
                       "--",
                       "/opt/tired-tests/tired_fixture",
                       "ignore-term",
                       NULL};
    REQUIRE(run(timeout, 0, &output, &error));
    REQUIRE(lifecycle("remove", "tired-qualification-timeout", 0, &output, &error));
    puts("PASS: notify readiness, forking/PIDFile and bounded stop timeout.");
    char *idle[] = {frontend,    "create", "--name",     "tired-qualification-idle",
                    "--profile", "none",   "--no-start", "--no-enable",
                    "--yes",     "--json", "--",         "/opt/tired-tests/tired_fixture",
                    "loop",      NULL};
    REQUIRE(run(idle, 0, &output, &error));
    REQUIRE(
        backend.query(backend.context, "tired-qualification-idle.service", true, &runtime, &error));
    REQUIRE(!runtime.running && !runtime.enabled);
    REQUIRE(lifecycle("enable", "tired-qualification-idle", 0, &output, &error));
    REQUIRE(
        backend.query(backend.context, "tired-qualification-idle.service", true, &runtime, &error));
    REQUIRE(!runtime.running && runtime.enabled);
    REQUIRE(lifecycle("remove", "tired-qualification-idle", 0, &output, &error));
    puts("PASS: installation, boot enablement and current activation remain distinct.");
    result = 0;
cleanup:
    if (account_session != NULL)
    {
        tired_process_cancel(account_session);
        while (!tired_process_step(account_session))
        {
            struct timespec pause = {.tv_nsec = 20000000};
            nanosleep(&pause, NULL);
        }
        tired_process_destroy(account_session);
    }
    if (policy_created && !owned_unlink("/etc/sudoers.d/tired-qualification"))
        result = 1;
    tired_runtime_destroy(&runtime);
    tired_backend_destroy(native);
    tired_layout_destroy(&layout);
    tired_text_destroy(&output);
    tired_text_destroy(&report);
    return result;
}
