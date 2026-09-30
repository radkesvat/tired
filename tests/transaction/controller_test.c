#define _GNU_SOURCE
#include "tired/file_fingerprint.h"
#include "tired/io.h"
#include "tired/mutation.h"
#include "tired/render.h"
#include "tired/service_record_storage.h"
#include "tired/transaction_inventory.h"
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
typedef struct
{
    TiredLayout *layout;
    char active_name[256], enabled_name[256];
    bool fail_start, fail_reload, uncertain, double_run;
    unsigned starts, stops, reloads, fail_starts_remaining;
    const char *crash_phase;
    unsigned crash_after;
    TiredEffectSink sink;
    void *sink_context;
    bool reject_job, ignore_stop;
} Fake;
static void tick(void *context, const char *phase)
{
    Fake *fake = context;
    if (fake->crash_phase != NULL && strcmp(fake->crash_phase, phase) == 0 &&
        --fake->crash_after == 0)
    {
        (void)raise(SIGKILL);
        _exit(98);
    }
}
static bool set(TiredText *text, const char *value, TiredError *error)
{
    return tired_text_set(text, value, strlen(value), TIRED_INPUT_LIMIT, error);
}
static bool query(void *context, const char *name, bool load, TiredRuntime *output,
                  TiredError *error)
{
    Fake *fake = context;
    TiredRuntime state = {0};
    TiredText path = {0};
    bool ok = tired_path_absolute(&fake->layout->paths[TIRED_PATH_UNITS], name, strlen(name), &path,
                                  error);
    struct stat file;
    state.found = ok && lstat(path.data, &file) == 0;
    state.loaded = state.found && load;
    state.job_known = true;
    state.active = state.running = strcmp(fake->active_name, name) == 0;
    state.enabled = strcmp(fake->enabled_name, name) == 0;
    state.failed = fake->fail_start && state.active;
    if (state.found && load)
        ok = set(&state.fragment, path.data, error) &&
             set(&state.active_state,
                 state.running && !state.failed ? "active"
                 : state.failed                 ? "failed"
                                                : "inactive",
                 error) &&
             set(&state.sub_state,
                 state.running && !state.failed ? "running"
                 : state.failed                 ? "auto-restart"
                                                : "dead",
                 error) &&
             set(&state.result, state.failed ? "exit-code" : "success", error);
    if (ok)
        ok = set(&state.file_state, state.enabled ? "enabled" : "disabled", error);
    state.running &= !state.failed;
    if (ok)
    {
        tired_runtime_destroy(output);
        *output = state;
        state = (TiredRuntime){0};
    }
    tired_runtime_destroy(&state);
    tired_text_destroy(&path);
    return ok;
}
static bool reload(void *context, bool *uncertain, TiredError *error)
{
    Fake *fake = context;
    ++fake->reloads;
    *uncertain = fake->uncertain;
    if (fake->fail_reload)
    {
        fake->fail_reload = false;
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "fixture-reload",
                               "Injected reload failure.", 0);
    }
    return true;
}
static bool enable(void *context, const char *name, bool value, bool *uncertain, TiredError *error)
{
    (void)error;
    Fake *fake = context;
    *uncertain = false;
    if (value)
        (void)snprintf(fake->enabled_name, sizeof(fake->enabled_name), "%s", name);
    else if (strcmp(fake->enabled_name, name) == 0)
        fake->enabled_name[0] = '\0';
    return true;
}
static bool job(void *context, const char *name, TiredJobAction action, bool *uncertain,
                TiredError *error)
{
    Fake *fake = context;
    *uncertain = false;
    char event[1024];
    int length = snprintf(event, sizeof(event),
                          "{\"unit\":\"%s\",\"job_id\":1,\"submitted\":true,\"accepted\":true,"
                          "\"finished\":false,\"rejected\":false,\"result\":\"\"}",
                          name);
    TiredText bytes = {.data = event, .length = length > 0 ? (size_t)length : 0};
    if (length <= 0 || (size_t)length >= sizeof(event) ||
        (fake->sink != NULL && !fake->sink(fake->sink_context, "job_accepted", &bytes, error)))
        return false;
    if (fake->reject_job)
        goto finished;
    if (action == TIRED_JOB_STOP)
    {
        ++fake->stops;
        if (!fake->ignore_stop && strcmp(fake->active_name, name) == 0)
            fake->active_name[0] = '\0';
    }
    else
    {
        ++fake->starts;
        fake->fail_start = fake->fail_starts_remaining != 0;
        if (fake->fail_starts_remaining != 0)
            --fake->fail_starts_remaining;
        fake->double_run |= fake->active_name[0] != '\0' && strcmp(fake->active_name, name) != 0;
        (void)snprintf(fake->active_name, sizeof(fake->active_name), "%s", name);
    }
    tick(context, "job_submitted");
finished:
    length = snprintf(event, sizeof(event),
                      "{\"unit\":\"%s\",\"job_id\":1,\"submitted\":true,\"accepted\":true,"
                      "\"finished\":true,\"rejected\":false,\"result\":\"%s\"}",
                      name, fake->reject_job ? "failed" : "done");
    bytes.length = length > 0 ? (size_t)length : 0;
    if (length <= 0 || (size_t)length >= sizeof(event) ||
        (fake->sink != NULL && !fake->sink(fake->sink_context, "job_finished", &bytes, error)))
        return false;
    tick(context, "job_finished");
    return !fake->reject_job ||
           tired_error_set(error, TIRED_RUNTIME_FAILED, "fixture-job", "Injected job failure.", 0);
}
static void effects(void *context, TiredEffectSink sink, void *sink_context)
{
    Fake *fake = context;
    fake->sink = sink;
    fake->sink_context = sink_context;
}
static bool verify(void *context, const TiredServiceSpec *spec, const TiredText *unit,
                   TiredError *error)
{
    (void)context;
    return tired_spec_validate_scalars(spec, error) && unit->length != 0;
}
static bool intent(TiredMutation *mutation, TiredTransactionOperation operation,
                   const TiredLayout *layout, TiredError *error)
{
    TiredServiceRecord record = {0};
    TiredText bytes = {0};
    TiredDirectory *directory = NULL;
    TiredFileFingerprint fingerprint = {0};
    char file[42];
    bool ok =
        tired_service_record_load(layout, mutation->proposed.metadata.service_uuid, &record, error);
    if (ok)
    {
        tired_service_record_destroy(&mutation->proposed);
        mutation->proposed = record;
        record = (TiredServiceRecord){0};
        mutation->operation = operation;
        mutation->now = mutation->defer = false;
        ok = set(&mutation->expected_unit_sha256, mutation->proposed.metadata.unit_sha256, error) &&
             tired_directory_open(layout->paths[TIRED_PATH_RECORDS].data, getuid(), true,
                                  &directory, error);
    }
    (void)snprintf(file, sizeof(file), "%s.json", mutation->proposed.metadata.service_uuid);
    if (ok)
        ok = tired_file_fingerprint(directory, file, TIRED_SERVICE_RECORD_LIMIT, &fingerprint,
                                    error) &&
             set(&mutation->expected_record_sha256, fingerprint.sha256, error) &&
             tired_uuid_create(mutation->proposed.metadata.transaction_uuid, error) &&
             tired_mutation_digest(mutation, mutation->proposed.review.approved_sha256, error);
    tired_directory_destroy(directory);
    tired_text_destroy(&bytes);
    tired_service_record_destroy(&record);
    return ok;
}
static bool remove_tree(const char *path)
{
    DIR *directory = opendir(path);
    if (directory == NULL)
        return false;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(directory)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[8192];
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat info;
        if (n < 0 || (size_t)n >= sizeof(child) || lstat(child, &info) != 0)
            ok = false;
        else if (S_ISDIR(info.st_mode))
            ok &= remove_tree(child);
        else
            ok &= unlink(child) == 0;
    }
    ok &= closedir(directory) == 0;
    return rmdir(path) == 0 && ok;
}
typedef struct
{
    TiredTransactionOperation operation;
    const char *phase;
    unsigned occurrence;
    bool no_start, reject_job, ignore_stop;
    TiredStatus expected;
} RecoveryCase;
static bool runtime_recovery_case(const TiredText *parent, const TiredPlan *plan,
                                  const TiredSettings *settings, const TiredTextList *risks,
                                  const RecoveryCase *test, unsigned index)
{
    int result = 1;
    TiredError error = {0};
    TiredLayout layout = {.user_scope = true};
    TiredText root = {0}, bytes = {0};
    TiredTextList paths = {0};
    TiredMutation mutation = {0};
    TiredOperationResult outcome = {0};
    TiredTransactionInventory inventory = {0};
    Fake *fake = NULL;
    char directory[32];
    (void)snprintf(directory, sizeof(directory), "runtime-%u", index);
    CHECK(tired_path_absolute(parent, directory, strlen(directory), &root, &error));
    const char *leaves[TIRED_PATH_COUNT] = {"config.json",  "profiles",          "environments",
                                            "units",        "records",           "history",
                                            "transactions", "run/operation.lock"};
    for (size_t i = 0; i < TIRED_PATH_COUNT; ++i)
        CHECK(tired_path_absolute(&root, leaves[i], strlen(leaves[i]), &layout.paths[i], &error));
    CHECK(tired_text_list_append(&paths, layout.paths[TIRED_PATH_UNITS].data,
                                 layout.paths[TIRED_PATH_UNITS].length, 256, TIRED_INPUT_LIMIT,
                                 &error));
    fake = mmap(NULL, sizeof(*fake), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(fake != MAP_FAILED);
    fake->layout = &layout;
    TiredBackend backend = {.context = fake,
                            .load_paths = &paths,
                            .features = {.systemd_version = 249},
                            .query = query,
                            .reload = reload,
                            .enable = enable,
                            .job = job,
                            .verify = verify,
                            .tick = tick,
                            .effects = effects};
    CHECK(tired_mutation_from_plan(plan, &layout, settings, risks, &mutation, &error));
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_OK && outcome.runtime.running);
    CHECK(intent(&mutation, test->operation, &layout, &error));
    if (test->operation == TIRED_TRANSACTION_EDIT || test->operation == TIRED_TRANSACTION_RESTORE)
    {
        CHECK(tired_uuid_create(mutation.proposed.metadata.revision_uuid, &error));
        CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_DESCRIPTION, "Recovered edit", 14,
                             TIRED_ORIGIN_USER, true, &error));
        if (test->no_start)
            CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_START, "false", 5,
                                 TIRED_ORIGIN_USER, true, &error));
        CHECK(tired_render_unit(&mutation.proposed.spec, mutation.proposed.metadata.service_uuid,
                                NULL, &mutation.proposed.credentials, &bytes, &error));
        CHECK(tired_digest_bytes(bytes.data, bytes.length, mutation.proposed.metadata.unit_sha256,
                                 &error));
    }
    CHECK(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
    unsigned starts = fake->starts, stops = fake->stops;
    fake->crash_phase = test->phase;
    fake->crash_after = test->occurrence;
    fake->reject_job = test->reject_job;
    fake->ignore_stop = test->ignore_stop;
    if (strcmp(test->phase, "observe") == 0)
        fake->fail_starts_remaining = 1;
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        (void)tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error);
        _exit(98);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
          WTERMSIG(status) == SIGKILL);
    fake->crash_phase = NULL;
    fake->reject_job = false;
    fake->fail_start = false; /* A later retry does not erase initial failure. */
    unsigned admitted_starts = fake->starts;
    CHECK(tired_mutation_recover(&layout, &backend, mutation.proposed.metadata.transaction_uuid,
                                 true, &outcome, &error));
    if (outcome.status != test->expected)
        fprintf(stderr, "Recovery case %u (%s/%u): status=%d error=%s\n", index, test->phase,
                test->occurrence, outcome.status,
                outcome.error.code == NULL ? "none" : outcome.error.code);
    CHECK(outcome.status == test->expected);
    CHECK(fake->starts ==
          admitted_starts + (!test->no_start && test->expected == TIRED_ROLLED_BACK ? 1U : 0U));
    CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
    if (test->expected == TIRED_RECOVERY_REQUIRED)
    {
        CHECK(outcome.recovery_required && inventory.pending_names.count != 0);
        /* Re-inspection cannot turn the old process into activation evidence. */
        CHECK(tired_mutation_recover(&layout, &backend, mutation.proposed.metadata.transaction_uuid,
                                     true, &outcome, &error));
        CHECK(outcome.status == TIRED_RECOVERY_REQUIRED && fake->starts == admitted_starts);
    }
    else
        CHECK(!outcome.recovery_required && inventory.pending_names.count == 0);
    if (test->no_start && test->expected == TIRED_OK)
        CHECK(!outcome.runtime.active && !outcome.runtime.running && fake->active_name[0] == '\0' &&
              fake->starts == starts && fake->stops == stops + 1);
    if (test->expected == TIRED_ROLLED_BACK)
        CHECK(outcome.rolled_back && outcome.runtime.running &&
              fake->starts == starts + (test->reject_job ? 1U : 0U) +
                                  (strcmp(test->phase, "observe") == 0 ? 2U : 0U));
    result = 0;
cleanup:
    if (fake != NULL && fake != MAP_FAILED)
        munmap(fake, sizeof(*fake));
    tired_transaction_inventory_destroy(&inventory);
    tired_operation_result_destroy(&outcome);
    tired_mutation_destroy(&mutation);
    tired_layout_destroy(&layout);
    tired_text_list_destroy(&paths);
    tired_text_destroy(&bytes);
    tired_text_destroy(&root);
    return result == 0;
}
int main(void)
{
    int result = 1;
    Fake *fake = NULL;
    char fixture[] = "controller-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredRequest request = {0};
    TiredPlan plan = {0};
    TiredMutation mutation = {0}, parsed = {0};
    TiredSettings settings = {0};
    TiredLayout layout = {.user_scope = true};
    TiredText root = {0}, bytes = {0}, output = {0};
    TiredTextList paths = {0}, risks = {0};
    TiredOperationResult outcome = {0};
    TiredError error = {0};
    TiredTransactionInventory inventory = {0};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText current = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&current, created, strlen(created), &root, &error));
    const char *leaves[TIRED_PATH_COUNT] = {"config.json",  "profiles",          "environments",
                                            "units",        "records",           "history",
                                            "transactions", "run/operation.lock"};
    for (size_t i = 0; i < TIRED_PATH_COUNT; ++i)
        CHECK(tired_path_absolute(&root, leaves[i], strlen(leaves[i]), &layout.paths[i], &error));
    CHECK(tired_text_list_append(&paths, layout.paths[TIRED_PATH_UNITS].data,
                                 layout.paths[TIRED_PATH_UNITS].length, 256, TIRED_INPUT_LIMIT,
                                 &error));
    fake = mmap(NULL, sizeof(*fake), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(fake != MAP_FAILED);
    fake->layout = &layout;
    TiredBackend backend = {.context = fake,
                            .load_paths = &paths,
                            .features = {.systemd_version = 249},
                            .query = query,
                            .reload = reload,
                            .enable = enable,
                            .job = job,
                            .verify = verify,
                            .tick = tick,
                            .effects = effects};
    const char *argv[] = {"tired", "--user", "--", "/usr/bin/sleep", "60"};
    CHECK(tired_cli_parse(5, argv, &request, &error));
    CHECK(tired_plan_prepare(&request, &plan, &error));
    tired_settings_defaults(&settings);
    settings.observation_usec = 0;
    CHECK(tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, &error));
    CHECK(tired_mutation_encode(&mutation, &bytes, &error));
    CHECK(tired_mutation_parse(bytes.data, bytes.length, &parsed, &error));
    CHECK(tired_mutation_apply(&parsed, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_OK && outcome.installed && outcome.runtime.running &&
          outcome.runtime.enabled);
    CHECK(tired_operation_output(&outcome, "create", true, &output, &error));
    CHECK(strstr(output.data, "not_verified") != NULL && fake->starts == 1);
    CHECK(!tired_mutation_apply(&parsed, &layout, &backend, &outcome, &error) &&
          error.status == TIRED_CONFLICT);
    CHECK(intent(&mutation, TIRED_TRANSACTION_DISABLE, &layout, &error));
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_OK && outcome.runtime.running && !outcome.runtime.enabled &&
          fake->stops == 0);
    CHECK(intent(&mutation, TIRED_TRANSACTION_STOP, &layout, &error));
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_OK && !outcome.runtime.running && fake->stops == 1);
    CHECK(intent(&mutation, TIRED_TRANSACTION_ENABLE, &layout, &error));
    mutation.now = true;
    CHECK(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_OK && outcome.runtime.running && outcome.runtime.enabled);
    CHECK(intent(&mutation, TIRED_TRANSACTION_EDIT, &layout, &error));
    mutation.defer = true;
    CHECK(tired_uuid_create(mutation.proposed.metadata.revision_uuid, &error));
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_DESCRIPTION, "Edited service", 14,
                         TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_render_unit(&mutation.proposed.spec, mutation.proposed.metadata.service_uuid, NULL,
                            &mutation.proposed.credentials, &bytes, &error));
    CHECK(tired_digest_bytes(bytes.data, bytes.length, mutation.proposed.metadata.unit_sha256,
                             &error));
    CHECK(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_OK && outcome.deferred && fake->starts == 2);
    CHECK(intent(&mutation, TIRED_TRANSACTION_EDIT, &layout, &error));
    CHECK(tired_uuid_create(mutation.proposed.metadata.revision_uuid, &error));
    CHECK(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
    fake->fail_reload = true;
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_ROLLED_BACK && outcome.rolled_back && !outcome.recovery_required);
    /* A new start failure restores the old control plane and its prior running
     * condition, then reports rollback rather than installing a failed edit. */
    CHECK(intent(&mutation, TIRED_TRANSACTION_EDIT, &layout, &error));
    CHECK(tired_uuid_create(mutation.proposed.metadata.revision_uuid, &error));
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_DESCRIPTION, "Failing revision", 16,
                         TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_render_unit(&mutation.proposed.spec, mutation.proposed.metadata.service_uuid, NULL,
                            &mutation.proposed.credentials, &bytes, &error));
    CHECK(tired_digest_bytes(bytes.data, bytes.length, mutation.proposed.metadata.unit_sha256,
                             &error));
    CHECK(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
    fake->fail_starts_remaining = 1;
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    CHECK(outcome.status == TIRED_ROLLED_BACK && outcome.rolled_back && !outcome.recovery_required);
    CHECK(strcmp(fake->active_name, mutation.proposed.metadata.unit_name.data) == 0);
    CHECK(intent(&mutation, TIRED_TRANSACTION_REMOVE, &layout, &error));
    CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
    if (outcome.status != TIRED_OK || outcome.installed || fake->active_name[0] ||
        fake->enabled_name[0])
        fprintf(stderr, "Removal result: %d %s installed=%d active=%s enabled=%s stops=%u\n",
                outcome.status, outcome.outcome, outcome.installed, fake->active_name,
                fake->enabled_name, fake->stops);
    CHECK(outcome.status == TIRED_OK && !outcome.installed && fake->active_name[0] == '\0' &&
          fake->enabled_name[0] == '\0');
    CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
    CHECK(inventory.complete && inventory.pending_names.count == 0 && !fake->double_run);
    /* Death before publication must clean only the recorded staging inode and
     * leave no installed service. Test both sides of the inode-link boundary. */
    const char *phases[] = {"prepare", "stage_recorded", "file_prepared"};
    for (size_t i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i)
    {
        CHECK(tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, &error));
        fake->crash_phase = phases[i];
        fake->crash_after = 1;
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0)
        {
            (void)tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error);
            _exit(98);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
              WTERMSIG(status) == SIGKILL);
        fake->crash_phase = NULL;
        CHECK(tired_mutation_recover(&layout, &backend, mutation.proposed.metadata.transaction_uuid,
                                     true, &outcome, &error));
        CHECK(outcome.status == TIRED_RECOVERY_REQUIRED && outcome.recovery_required);
        CHECK(tired_mutation_recover(&layout, &backend, mutation.proposed.metadata.transaction_uuid,
                                     false, &outcome, &error));
        CHECK(outcome.status == TIRED_OK && outcome.rolled_back && !outcome.installed);
        CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
        CHECK(inventory.complete && inventory.pending_names.count == 0);
    }
    /* Each admitted phase is interrupted before and after its external action.
     * Recovery may confirm failure, but must never replay an admitted start. */
    const char *actions[] = {"prepared", "publish_files", "reload",       "enable",
                             "start",    "observe",       "store_record", "commit"};
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
        for (unsigned occurrence = 1; occurrence <= 2; ++occurrence)
        {
            if (i == 0 && occurrence == 2)
                continue; /* Full preparation has one boundary, before publication. */
            if (strcmp(actions[i], "start") == 0 && occurrence == 1)
                continue; /* Unsubmitted activation stays pending; covered in its own scope. */
            CHECK(tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, &error));
            fake->crash_phase = actions[i];
            fake->crash_after = occurrence;
            unsigned starts = fake->starts;
            pid_t child = fork();
            CHECK(child >= 0);
            if (child == 0)
            {
                (void)tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error);
                _exit(98);
            }
            int status;
            CHECK(waitpid(child, &status, 0) == child);
            if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
                fprintf(stderr, "Crash phase %s/%u did not occur: status=%d\n", actions[i],
                        occurrence, status);
            CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
            fake->crash_phase = NULL;
            CHECK(tired_mutation_recover(&layout, &backend,
                                         mutation.proposed.metadata.transaction_uuid, true,
                                         &outcome, &error));
            CHECK(outcome.status == TIRED_OK || outcome.status == TIRED_RUNTIME_FAILED);
            CHECK(outcome.installed && !outcome.recovery_required && fake->starts <= starts + 1);
            CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
            CHECK(inventory.complete && inventory.pending_names.count == 0);
            CHECK(intent(&mutation, TIRED_TRANSACTION_REMOVE, &layout, &error));
            CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
            CHECK(outcome.status == TIRED_OK && !outcome.installed && !fake->double_run);
        }
    /* Cleanup has a durable deletion ledger independent of retired journals.
     * Kill during removal after deleting both descriptors and journal entries;
     * selected recovery must finish without submitting another workload job. */
    const unsigned cleanup_steps[] = {1, 4, 9};
    for (size_t i = 0; i < sizeof(cleanup_steps) / sizeof(cleanup_steps[0]); ++i)
    {
        CHECK(tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, &error));
        CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_START, "false", 5,
                             TIRED_ORIGIN_USER, true, &error));
        CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_ENABLE, "false", 5,
                             TIRED_ORIGIN_USER, true, &error));
        CHECK(tired_mutation_digest(&mutation, mutation.proposed.review.approved_sha256, &error));
        CHECK(tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error));
        CHECK(outcome.status == TIRED_OK);
        CHECK(intent(&mutation, TIRED_TRANSACTION_REMOVE, &layout, &error));
        fake->crash_phase = "cleanup_entry";
        fake->crash_after = cleanup_steps[i];
        unsigned starts = fake->starts;
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0)
        {
            (void)tired_mutation_apply(&mutation, &layout, &backend, &outcome, &error);
            _exit(98);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
              WTERMSIG(status) == SIGKILL);
        fake->crash_phase = NULL;
        CHECK(tired_mutation_recover(&layout, &backend, mutation.proposed.metadata.transaction_uuid,
                                     true, &outcome, &error));
        CHECK(outcome.status == TIRED_OK && !outcome.installed && fake->starts == starts);
        CHECK(tired_transaction_inventory_load(&layout, &inventory, &error));
        CHECK(inventory.complete && inventory.pending_names.count == 0);
    }
    const RecoveryCase recovery_cases[] = {
        {TIRED_TRANSACTION_RESTART, "restart", 1, false, false, false, TIRED_RECOVERY_REQUIRED},
        {TIRED_TRANSACTION_EDIT, "restart", 1, false, false, false, TIRED_RECOVERY_REQUIRED},
        {TIRED_TRANSACTION_RESTART, "job_submitted", 1, false, false, false,
         TIRED_RECOVERY_REQUIRED},
        {TIRED_TRANSACTION_EDIT, "job_submitted", 1, false, false, false, TIRED_RECOVERY_REQUIRED},
        {TIRED_TRANSACTION_RESTART, "job_finished", 1, false, false, false, TIRED_OK},
        {TIRED_TRANSACTION_EDIT, "job_finished", 1, false, false, false, TIRED_OK},
        {TIRED_TRANSACTION_RESTART, "restart", 2, false, false, false, TIRED_OK},
        {TIRED_TRANSACTION_EDIT, "restart", 2, false, false, false, TIRED_OK},
        {TIRED_TRANSACTION_RESTART, "job_finished", 1, false, true, false, TIRED_RUNTIME_FAILED},
        {TIRED_TRANSACTION_EDIT, "job_finished", 1, false, true, false, TIRED_ROLLED_BACK},
        {TIRED_TRANSACTION_EDIT, "observe", 2, false, false, false, TIRED_ROLLED_BACK},
        {TIRED_TRANSACTION_EDIT, "prepared", 1, true, false, false, TIRED_OK},
        {TIRED_TRANSACTION_RESTORE, "prepared", 1, true, false, false, TIRED_OK},
        {TIRED_TRANSACTION_EDIT, "stop", 1, true, false, false, TIRED_OK},
        {TIRED_TRANSACTION_EDIT, "stop", 2, true, false, false, TIRED_OK},
        {TIRED_TRANSACTION_EDIT, "stop", 2, true, false, true, TIRED_ROLLED_BACK},
    };
    for (size_t i = 0; i < sizeof(recovery_cases) / sizeof(recovery_cases[0]); ++i)
        CHECK(runtime_recovery_case(&root, &plan, &settings, &risks, &recovery_cases[i],
                                    (unsigned)i));
    result = 0;
cleanup:
    if (fake != NULL && fake != MAP_FAILED)
        munmap(fake, sizeof(*fake));
    tired_transaction_inventory_destroy(&inventory);
    tired_operation_result_destroy(&outcome);
    tired_mutation_destroy(&mutation);
    tired_mutation_destroy(&parsed);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    tired_settings_destroy(&settings);
    tired_layout_destroy(&layout);
    tired_text_list_destroy(&paths);
    tired_text_list_destroy(&risks);
    tired_text_destroy(&bytes);
    tired_text_destroy(&output);
    if (created != NULL && !remove_tree(root.data))
    {
        fprintf(stderr, "Retained fixture: %s\n", root.data);
        result = 1;
    }
    tired_text_destroy(&root);
    free(cwd);
    return result;
}
