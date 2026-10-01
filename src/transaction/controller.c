#include "tired/backup_ledger.h"
#include "tired/encode.h"
#include "tired/file_backup.h"
#include "tired/file_change.h"
#include "tired/file_reconciliation.h"
#include "tired/history.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/manifest_storage.h"
#include "tired/memory.h"
#include "tired/mutation.h"
#include "tired/private_file.h"
#include "tired/render.h"
#include "tired/service_files.h"
#include "tired/service_record_storage.h"
#include "tired/transaction_journal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    const TiredLayout *layout;
    const TiredBackend *backend;
    TiredDirectory *runtime_directory, *transactions, *transaction, *journal, *artifacts;
    TiredOperationLock *lock;
    TiredFileManifest manifest;
    TiredFileChange files[5];
    TiredServiceRecord old;
    TiredRuntime prior;
    unsigned effect_sequence, effect_count;
    bool accepted, prepared, executed, files_published, new_enabled, retiring_old;
} Controller;
static void destroy(Controller *controller)
{
    for (size_t i = 0; i < controller->manifest.count; ++i)
        tired_text_destroy(&controller->files[i].target.unit_name);
    tired_text_destroy(&controller->manifest.prepared.unit_name);
    tired_service_record_destroy(&controller->old);
    tired_runtime_destroy(&controller->prior);
    tired_directory_destroy(controller->artifacts);
    tired_directory_destroy(controller->journal);
    tired_directory_destroy(controller->transaction);
    tired_directory_destroy(controller->transactions);
    tired_operation_lock_destroy(controller->lock);
    tired_directory_destroy(controller->runtime_directory);
}
static bool append(Controller *controller, TiredTransactionAction action,
                   TiredTransactionActionState state, TiredError *error)
{
    TiredTransactionJournal journal = {0};
    bool ok = tired_transaction_journal_read(controller->journal, &journal, error);
    if (ok)
    {
        TiredTransactionRecord record = controller->manifest.prepared;
        record.sequence = journal.count + 1;
        record.action = action;
        record.state = state;
        ok = tired_transaction_journal_append_atomic(controller->journal, controller->lock, &record,
                                                     error);
    }
    tired_transaction_journal_destroy(&journal);
    if (controller->backend->tick != NULL)
        controller->backend->tick(controller->backend->context,
                                  tired_transaction_action_name(action));
    return ok;
}
static bool effect(void *context, const char *kind, const TiredText *bytes, TiredError *error)
{
    Controller *controller = context;
    TiredTransactionJournal journal = {0};
    struct json_object *event = NULL, *details = NULL;
    bool ok = tired_transaction_journal_read(controller->journal, &journal, error) &&
              journal.progress.pending &&
              tired_json_parse(bytes->data, bytes->length, TIRED_INPUT_LIMIT, &details, error);
    if (ok)
    {
        if (controller->effect_sequence != journal.count)
        {
            controller->effect_sequence = (unsigned)journal.count;
            controller->effect_count = 0;
        }
        event = json_object_new_object();
        ok = event != NULL;
        if (ok)
        {
            json_object_object_add(event, "event_type", json_object_new_string(kind));
            ok = json_object_object_add(event, "details", details) == 0;
            if (ok)
                details = NULL;
        }
        char name[32];
        (void)snprintf(name, sizeof(name), "effect-%04u-%u.json", controller->effect_sequence,
                       ++controller->effect_count);
        const char *encoded =
            ok ? json_object_to_json_string_ext(event, JSON_C_TO_STRING_PLAIN) : NULL;
        ok = encoded != NULL && controller->effect_count <= 2 &&
             tired_private_file_create(controller->transaction, name, encoded, strlen(encoded),
                                       error);
    }
    json_object_put(event);
    json_object_put(details);
    tired_transaction_journal_destroy(&journal);
    return ok;
}
static bool finish_action(Controller *controller, TiredTransactionAction action, bool ok,
                          bool uncertain, TiredError *error)
{
    TiredError original = *error;
    bool recorded = append(controller, action,
                           ok          ? TIRED_ACTION_COMPLETED
                           : uncertain ? TIRED_ACTION_UNCERTAIN
                                       : TIRED_ACTION_FAILED,
                           error);
    if (recorded && !ok)
        *error = original;
    return recorded && ok;
}
static bool reload(Controller *controller, TiredError *error)
{
    bool uncertain = false;
    if (!append(controller, TIRED_ACTION_RELOAD, TIRED_ACTION_INTENT, error))
        return false;
    bool ok = controller->backend->reload(controller->backend->context, &uncertain, error);
    return finish_action(controller, TIRED_ACTION_RELOAD, ok, uncertain, error);
}
static bool enable(Controller *controller, const char *unit, bool value, TiredError *error)
{
    bool uncertain = false;
    TiredTransactionAction action = value ? TIRED_ACTION_ENABLE : TIRED_ACTION_DISABLE;
    if (!append(controller, action, TIRED_ACTION_INTENT, error))
        return false;
    bool ok =
        controller->backend->enable(controller->backend->context, unit, value, &uncertain, error);
    if (value && ok)
        controller->new_enabled = true;
    return finish_action(controller, action, ok, uncertain, error);
}
static bool reset_failure(Controller *controller, const char *unit, TiredError *error)
{
    if (controller->backend->reset_failed == NULL)
        return true;
    bool uncertain = false;
    if (!append(controller, TIRED_ACTION_RESET_FAILED, TIRED_ACTION_INTENT, error))
        return false;
    bool reset =
        controller->backend->reset_failed(controller->backend->context, unit, &uncertain, error);
    return finish_action(controller, TIRED_ACTION_RESET_FAILED, reset, uncertain, error);
}
static bool job(Controller *controller, const char *unit, TiredJobAction action, TiredError *error)
{
    static const TiredTransactionAction actions[] = {TIRED_ACTION_START, TIRED_ACTION_STOP,
                                                     TIRED_ACTION_RESTART};
    bool uncertain = false;
    if (action != TIRED_JOB_STOP && controller->backend->reset_failed != NULL)
    {
        TiredRuntime before = {0};
        bool read =
            controller->backend->query(controller->backend->context, unit, true, &before, error);
        /* Baseline managers can retain the preceding exit-code Result when
         * their retry budget expires. An explicit activation of a failed unit
         * resets only that unit, without guessing the preserved failure cause. */
        bool limited = read && before.failed;
        tired_runtime_destroy(&before);
        if (!read)
            return false;
        if (limited && !reset_failure(controller, unit, error))
            return false;
    }
    if (!append(controller, actions[action], TIRED_ACTION_INTENT, error))
        return false;
    /* Once a workload job is admitted, file rollback cannot erase its effects. */
    bool prior_execution = controller->executed;
    controller->executed = true;
    bool ok =
        controller->backend->job(controller->backend->context, unit, action, &uncertain, error);
    if (controller->backend->execution_possible != NULL)
        controller->executed = prior_execution || controller->backend->execution_possible(
                                                      controller->backend->context);
    return finish_action(controller, actions[action], ok, uncertain, error);
}
static bool record_stage(void *context, const char *uuid, const TiredFileFingerprint *identity,
                         TiredError *error)
{
    Controller *controller = context;
    TiredFileChange *file = &controller->files[controller->manifest.count - 1];
    TiredText allocated = {0}, before = {0};
    struct json_object *document = json_object_new_object(), *a = NULL, *b = NULL;
    bool ok = document != NULL && tired_file_fingerprint_encode(identity, &allocated, error) &&
              tired_file_fingerprint_encode(&file->before, &before, error) &&
              tired_json_parse(allocated.data, allocated.length, 4096, &a, error) &&
              tired_json_parse(before.data, before.length, 4096, &b, error);
    if (ok)
    {
        json_object_object_add(document, "role", json_object_new_int(file->target.role));
        json_object_object_add(document, "unit_name",
                               json_object_new_string(file->target.unit_name.data == NULL
                                                          ? ""
                                                          : file->target.unit_name.data));
        json_object_object_add(document, "revision_uuid",
                               json_object_new_string(file->target.revision_uuid));
        json_object_object_add(document, "staging_uuid", json_object_new_string(uuid));
        json_object_object_add(document, "identity", a);
        a = NULL;
        json_object_object_add(document, "before", b);
        b = NULL;
        char name[32];
        (void)snprintf(name, sizeof(name), "stage-%zu.json", controller->manifest.count);
        const char *bytes = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
        ok = bytes != NULL &&
             tired_private_file_create(controller->transaction, name, bytes, strlen(bytes), error);
    }
    json_object_put(a);
    json_object_put(b);
    json_object_put(document);
    tired_text_destroy(&allocated);
    tired_text_destroy(&before);
    if (ok && controller->backend->tick != NULL)
        controller->backend->tick(controller->backend->context, "stage_recorded");
    return ok;
}
static bool prepare_backup(Controller *controller, TiredDirectory *source, const char *name,
                           TiredFileChange *file, TiredError *error)
{
    TiredText encoded = {0}, bytes = {0};
    TiredFileFingerprint snapshot = {0};
    struct json_object *document = json_object_new_object(), *before = NULL;
    bool ok = document != NULL && tired_uuid_create(file->rollback_uuid, error) &&
              tired_file_fingerprint_encode(&file->before, &encoded, error) &&
              tired_json_parse(encoded.data, encoded.length, 4096, &before, error);
    if (ok)
    {
        ok = json_object_object_add(document, "before", before) == 0;
        if (ok)
            before = NULL;
        struct json_object *uuid = json_object_new_string(file->rollback_uuid);
        if (uuid == NULL || json_object_object_add(document, "rollback_uuid", uuid) != 0)
        {
            json_object_put(uuid);
            ok = false;
        }
        const char *ledger =
            ok ? json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN) : NULL;
        char filename[32];
        (void)snprintf(filename, sizeof(filename), "backup-%zu.json", controller->manifest.count);
        ok = ledger != NULL && tired_private_file_create(controller->transaction, filename, ledger,
                                                         strlen(ledger), error);
    }
    if (ok && controller->backend->tick != NULL)
        controller->backend->tick(controller->backend->context, "backup_recorded");
    ok = ok &&
         tired_file_snapshot(source, name, TIRED_PRIVATE_FILE_LIMIT, &snapshot, &bytes, error) &&
         tired_file_fingerprint_equal(&snapshot, &file->before) &&
         tired_private_file_create(controller->artifacts, file->rollback_uuid, bytes.data,
                                   bytes.length, error) &&
         tired_file_fingerprint(source, name, TIRED_PRIVATE_FILE_LIMIT, &snapshot, error) &&
         tired_file_fingerprint_equal(&snapshot, &file->before);
    if (bytes.data != NULL)
        tired_memory_clear(bytes.data, bytes.length);
    tired_text_destroy(&bytes);
    tired_text_destroy(&encoded);
    json_object_put(before);
    json_object_put(document);
    if (ok && controller->backend->tick != NULL)
        controller->backend->tick(controller->backend->context, "backup_prepared");
    return ok || (error->status != TIRED_OK
                      ? false
                      : tired_error_set(error, TIRED_CONFLICT, "backup-source-changed",
                                        "Rollback source changed during preparation.", 0));
}
static bool prepare_file(Controller *controller, TiredFileTargetRole role, const char *name,
                         const char *revision, const TiredText *contents, TiredError *error)
{
    if (controller->manifest.count >= 5)
        return tired_error_set(error, TIRED_INTERNAL, "file-count",
                               "Too many control-plane changes.", 0);
    TiredFileChange *file = &controller->files[controller->manifest.count++];
    file->target.role = role;
    memcpy(file->target.service_uuid, controller->manifest.prepared.service_uuid, 37);
    if (role == TIRED_FILE_TARGET_UNIT &&
        !tired_text_set(&file->target.unit_name, name, strlen(name), 255, error))
        return false;
    if (role == TIRED_FILE_TARGET_ENVIRONMENT)
        memcpy(file->target.revision_uuid, revision, 37);
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL;
    TiredPublication *publication = NULL;
    bool ok = false;
    if (!tired_file_target_resolve(controller->layout, &file->target, &resolved, error) ||
        !tired_directory_ensure(resolved.directory.data, resolved.private_directory, &directory,
                                error) ||
        !tired_file_fingerprint(directory, resolved.name.data, TIRED_PRIVATE_FILE_LIMIT,
                                &file->before, error))
        goto done;
    if (file->before.exists)
    {
        if (!prepare_backup(controller, directory, resolved.name.data, file, error))
            goto done;
    }
    if (contents != NULL)
    {
        if (!tired_publication_prepare_recorded(directory, resolved.name.data, contents->data,
                                                contents->length, resolved.mode, record_stage,
                                                controller, &publication, error))
            goto done;
        const char *temporary = tired_publication_temporary_name(publication);
        memcpy(file->staging_uuid, temporary + 7, 36);
        file->staging_uuid[36] = '\0';
        if (!tired_file_fingerprint(directory, temporary, TIRED_PRIVATE_FILE_LIMIT, &file->after,
                                    error))
            goto done;
    }
    ok = true;
    if (controller->backend->tick != NULL)
        controller->backend->tick(controller->backend->context, "file_prepared");
done:
    tired_publication_destroy(publication);
    tired_directory_destroy(directory);
    tired_resolved_file_destroy(&resolved);
    return ok;
}
static bool file_phase(Controller *controller, bool records_only, bool rollback, TiredError *error)
{
    TiredTransactionAction action =
        records_only ? TIRED_ACTION_STORE_RECORD
        : controller->manifest.prepared.operation == TIRED_TRANSACTION_REMOVE
            ? TIRED_ACTION_REMOVE_FILES
            : TIRED_ACTION_PUBLISH_FILES;
    if (!append(controller, action, TIRED_ACTION_INTENT, error))
        return false;
    TiredFileOrder order = {0};
    bool ok = tired_file_change_order(&controller->manifest, rollback, &order, error);
    for (size_t i = 0; ok && i < order.count; ++i)
    {
        size_t index = order.indices[i];
        const TiredFileChange *file = &controller->files[index];
        if ((file->target.role == TIRED_FILE_TARGET_RECORD) != records_only)
            continue;
        if (!rollback && !records_only &&
            controller->manifest.prepared.operation == TIRED_TRANSACTION_RENAME &&
            file->target.role == TIRED_FILE_TARGET_UNIT && file->before.exists &&
            !file->after.exists && !controller->retiring_old)
            continue;
        if (rollback)
        {
            TiredResolvedFile resolved = {0};
            TiredDirectory *directory = NULL;
            TiredFileFingerprint current = {0};
            ok = tired_file_target_resolve(controller->layout, &file->target, &resolved, error) &&
                 tired_directory_open(resolved.directory.data, geteuid(),
                                      resolved.private_directory, &directory, error) &&
                 tired_file_fingerprint(directory, resolved.name.data, TIRED_PRIVATE_FILE_LIMIT,
                                        &current, error);
            bool unchanged = ok && tired_file_fingerprint_equal(&current, &file->before);
            tired_directory_destroy(directory);
            tired_resolved_file_destroy(&resolved);
            if (!ok || unchanged)
                continue;
        }
        TiredFileChangeResult step = {0};
        ok = tired_file_change_apply(controller->layout, &controller->manifest, index, rollback,
                                     controller->lock, &step, error);
        controller->files_published |= step.reached;
    }
    return finish_action(controller, action, ok, !ok, error);
}
static bool retire_old(Controller *controller, TiredError *error)
{
    if (!append(controller, TIRED_ACTION_REMOVE_FILES, TIRED_ACTION_INTENT, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < controller->manifest.count && ok; ++i)
    {
        const TiredFileChange *file = &controller->files[i];
        if (file->target.role == TIRED_FILE_TARGET_UNIT && file->before.exists &&
            !file->after.exists)
        {
            TiredFileChangeResult result = {0};
            ok = tired_file_change_apply(controller->layout, &controller->manifest, i, false,
                                         controller->lock, &result, error);
        }
    }
    controller->retiring_old = ok;
    return finish_action(controller, TIRED_ACTION_REMOVE_FILES, ok, !ok, error);
}
static bool observe(Controller *controller, const TiredMutation *mutation, const char *unit,
                    bool expect_running, TiredRuntime *state, TiredError *error)
{
    if (!append(controller, TIRED_ACTION_OBSERVE, TIRED_ACTION_INTENT, error))
        return false;
    bool ok = true;
    uint64_t deadline = tired_monotonic_usec() + (expect_running ? mutation->observation_usec : 0);
    bool restarts_known = false;
    uint64_t first_restarts = 0;
    bool expect_stopped =
        mutation->operation == TIRED_TRANSACTION_STOP ||
        (mutation->operation == TIRED_TRANSACTION_DISABLE && mutation->now) ||
        ((mutation->operation == TIRED_TRANSACTION_EDIT ||
          mutation->operation == TIRED_TRANSACTION_RESTORE) &&
         !mutation->defer && !mutation->proposed.spec.fields[TIRED_FIELD_START].value.boolean);
    do
    {
        if (!controller->backend->query(controller->backend->context, unit, true, state, error))
        {
            ok = false;
            break;
        }
        if (!restarts_known && state->restarts_known)
        {
            restarts_known = true;
            bool same_context =
                controller->prior.active && controller->prior.restarts_known &&
                (mutation->operation == TIRED_TRANSACTION_START ||
                 (mutation->operation == TIRED_TRANSACTION_ENABLE && mutation->now));
            first_restarts =
                expect_running ? same_context ? controller->prior.restarts : 0 : state->restarts;
        }
        bool completes =
            tired_spec_choice_is(&mutation->proposed.spec, TIRED_FIELD_TYPE, "oneshot") ||
            mutation->proposed.spec.fields[TIRED_FIELD_REMAIN_AFTER_EXIT].value.boolean;
        if (expect_running &&
            (state->failed || (restarts_known && state->restarts > first_restarts) ||
             (!state->running && !(completes && state->completed))))
        {
            ok = tired_error_set(error, TIRED_RUNTIME_FAILED, "initial-runtime-failure",
                                 "Installed workload failed, restarted or did not reach the "
                                 "requested initial state.",
                                 0);
            break;
        }
        if (expect_stopped &&
            (!state->job_known || state->job_id != 0 || state->active || state->running ||
             (state->loaded && (state->active_state.data == NULL ||
                                (strcmp(state->active_state.data, "inactive") != 0 &&
                                 strcmp(state->active_state.data, "failed") != 0)))))
        {
            ok = tired_error_set(error, TIRED_RUNTIME_FAILED, "requested-state-not-observed",
                                 "The requested inactive service state was not observed. Inspect "
                                 "status and retry after its current job completes.",
                                 0);
            break;
        }
        if (!expect_running || tired_monotonic_usec() >= deadline)
            break;
        struct timespec pause = {.tv_nsec = 100000000};
        (void)nanosleep(&pause, NULL);
    } while (true);
    return finish_action(controller, TIRED_ACTION_OBSERVE, ok, false, error);
}
static bool pending_uncertain(Controller *controller)
{
    TiredTransactionJournal journal = {0};
    TiredError ignored = {0};
    bool uncertain = !tired_transaction_journal_read(controller->journal, &journal, &ignored) ||
                     journal.progress.pending;
    tired_transaction_journal_destroy(&journal);
    return uncertain;
}
static bool rollback(Controller *controller, const TiredMutation *mutation, bool resuming,
                     TiredError *error)
{
    if (pending_uncertain(controller) ||
        (!resuming && !append(controller, TIRED_ACTION_ROLLBACK, TIRED_ACTION_INTENT, error)))
        return false;
    const char *new_name = mutation->proposed.metadata.unit_name.data;
    const char *old_name = controller->old.metadata.unit_name.data;
    bool restored_context = false;
    if (resuming && old_name != NULL && strcmp(old_name, new_name) == 0)
    {
        TiredServiceFiles old_files = {0};
        restored_context =
            tired_service_files_inspect(controller->layout, &controller->old, &old_files, error) &&
            old_files.unit.state == TIRED_SERVICE_FILE_MATCH;
        tired_error_clear(error);
    }
    if (controller->executed && !restored_context)
    {
        TiredRuntime current = {0};
        bool ok = controller->backend->query(controller->backend->context, new_name, false,
                                             &current, error);
        bool active = current.active;
        tired_runtime_destroy(&current);
        if (!ok || (active && !job(controller, new_name, TIRED_JOB_STOP, error)))
            return false;
    }
    if ((controller->new_enabled || resuming) && !enable(controller, new_name, false, error))
        return false;
    if (!file_phase(controller, true, true, error) || !file_phase(controller, false, true, error) ||
        !reload(controller, error))
        return false;
    if (mutation->operation != TIRED_TRANSACTION_CREATE)
    {
        if (!enable(controller, old_name, controller->prior.enabled, error))
            return false;
        TiredRuntime restored = {0};
        TiredMutation previous = *mutation;
        previous.proposed = controller->old;
        previous.operation = TIRED_TRANSACTION_START;
        previous.root_previously_selected = controller->old.metadata.service_uid == 0;
        previous.recovery = true;
        previous.restore_drift = false;
        if (controller->prior.active)
        {
            TiredServiceFiles restored_files = {0};
            TiredRiskReport risks = {0};
            bool safe = tired_service_files_inspect(controller->layout, &controller->old,
                                                    &restored_files, error) &&
                        restored_files.unit.state == TIRED_SERVICE_FILE_MATCH &&
                        (!controller->old.has_environment ||
                         restored_files.environment.state == TIRED_SERVICE_FILE_MATCH) &&
                        tired_mutation_workload_check(&previous, controller->layout,
                                                      controller->backend, &risks, error);
            for (size_t i = 0; safe && i < TIRED_RISK_COUNT; ++i)
                safe = !risks.pending[i] &&
                       (!risks.present[i] || controller->old.review.acknowledged[i]);
            if (!safe)
                return tired_error_set(error, TIRED_RECOVERY_REQUIRED,
                                       "previous-runtime-review-required",
                                       "Previous files were restored, but executing them requires "
                                       "a fresh review of their current contents and privileges.",
                                       0);
            bool read = controller->backend->query(controller->backend->context, old_name, true,
                                                   &restored, error);
            bool active = restored.active;
            tired_runtime_destroy(&restored);
            if (!read || (!active && !job(controller, old_name, TIRED_JOB_START, error)))
                return false;
        }
        bool ok =
            observe(controller, &previous, old_name, controller->prior.active, &restored, error);
        tired_runtime_destroy(&restored);
        if (!ok)
            return false;
    }
    return append(controller, TIRED_ACTION_ROLLBACK, TIRED_ACTION_COMPLETED, error);
}
static bool save_history(Controller *controller, TiredError *error)
{
    if (controller->old.metadata.service_uuid[0] == '\0')
        return true;
    TiredDirectory *root = NULL, *service = NULL, *revision = NULL;
    TiredText bytes = {0}, unit = {0};
    bool ok = tired_directory_ensure(controller->layout->paths[TIRED_PATH_HISTORY].data, true,
                                     &root, error) &&
              tired_directory_child(root, controller->old.metadata.service_uuid, true, true,
                                    &service, error) &&
              tired_directory_child(service, controller->old.metadata.revision_uuid, true, true,
                                    &revision, error);
    if (ok)
    {
        TiredFileFingerprint current = {0};
        ok = tired_file_fingerprint(revision, "record.json", TIRED_PRIVATE_FILE_LIMIT, &current,
                                    error);
        if (ok && !current.exists)
            ok =
                tired_service_record_encode(&controller->old, &bytes, error) &&
                tired_private_file_create(revision, "record.json", bytes.data, bytes.length, error);
        if (ok)
        {
            ok = tired_file_fingerprint(revision, "unit.service", TIRED_PRIVATE_FILE_LIMIT,
                                        &current, error);
            if (ok && !current.exists)
                ok = tired_render_unit(
                         &controller->old.spec, controller->old.metadata.service_uuid,
                         controller->old.has_environment ? &controller->old.environment_path : NULL,
                         &controller->old.credentials, &unit, error) &&
                     tired_private_file_create(revision, "unit.service", unit.data, unit.length,
                                               error);
        }
    }
    for (size_t i = 0; ok && i < controller->manifest.count; ++i)
    {
        const TiredFileChange *file = &controller->files[i];
        if (file->target.role != TIRED_FILE_TARGET_UNIT || !file->before.exists ||
            strcmp(file->before.sha256, controller->old.metadata.unit_sha256) == 0)
            continue;
        char descriptor[96];
        int length =
            snprintf(descriptor, sizeof(descriptor), "{\"sha256\":\"%s\"}\n", file->before.sha256);
        TiredFileFingerprint existing = {0};
        ok = length > 0 && (size_t)length < sizeof(descriptor) &&
             tired_file_fingerprint(revision, "foreign-unit.json", 128, &existing, error);
        if (ok && !existing.exists)
            ok = tired_private_file_create(revision, "foreign-unit.json", descriptor,
                                           (size_t)length, error);
        if (ok)
            ok = tired_file_fingerprint(revision, "foreign-unit.service", TIRED_PRIVATE_FILE_LIMIT,
                                        &existing, error);
        if (ok && !existing.exists)
            ok = tired_private_file_read(controller->artifacts, file->rollback_uuid,
                                         TIRED_PRIVATE_FILE_LIMIT, &unit, error) &&
                 tired_private_file_create(revision, "foreign-unit.service", unit.data, unit.length,
                                           error);
    }
    tired_text_destroy(&bytes);
    tired_text_destroy(&unit);
    tired_directory_destroy(revision);
    tired_directory_destroy(service);
    tired_directory_destroy(root);
    return ok;
}
static bool cleanup_entry(TiredDirectory *directory, const char *name,
                          const TiredFileFingerprint *expected, TiredError *error)
{
    TiredFileFingerprint actual = {0};
    if (!tired_file_fingerprint(directory, name, TIRED_PRIVATE_FILE_LIMIT, &actual, error))
        return false;
    if (!actual.exists)
        return true;
    if (!expected->exists || !tired_file_fingerprint_equal(&actual, expected))
        return tired_error_set(error, TIRED_CONFLICT, "retained-artifact-changed",
                               "A retained transaction artifact changed; it was not deleted.", 0);
    if (!tired_directory_check(directory, error))
        return false;
    if (unlinkat(tired_directory_fd(directory), name, 0) != 0 ||
        fsync(tired_directory_fd(directory)) != 0)
        return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "artifact-cleanup",
                               "Cannot finish checked transaction-artifact cleanup.", errno);
    return true;
}
static bool cleanup_files(Controller *controller, bool rolled_back, TiredError *error)
{
    for (size_t i = 0; i < controller->manifest.count; ++i)
    {
        const TiredFileChange *file = &controller->files[i];
        TiredResolvedFile resolved = {0};
        TiredDirectory *directory = NULL;
        bool ok = tired_file_target_resolve(controller->layout, &file->target, &resolved, error) &&
                  tired_directory_open(resolved.directory.data, geteuid(),
                                       resolved.private_directory, &directory, error);
        if (ok && file->after.exists)
        {
            char name[64];
            (void)snprintf(name, sizeof(name), ".tired-%s.tmp", file->staging_uuid);
            ok = cleanup_entry(directory, name,
                               rolled_back || !file->before.exists ? &file->after : &file->before,
                               error);
            if (ok && rolled_back && !file->before.exists)
            {
                (void)snprintf(name, sizeof(name), ".tired-%s.removed", file->staging_uuid);
                ok = cleanup_entry(directory, name, &file->after, error);
            }
        }
        if (ok && file->before.exists && !file->after.exists)
        {
            char name[64];
            (void)snprintf(name, sizeof(name), ".tired-%s.removed", file->rollback_uuid);
            ok = cleanup_entry(directory, name, &file->before, error);
        }
        tired_directory_destroy(directory);
        tired_resolved_file_destroy(&resolved);
        if (!ok)
            return false;
    }
    return true;
}
/* No destination is published until files.json is durable. These records pin
 * each staging inode before its name is linked, including interrupted writes. */
static bool rollback_preparation(Controller *controller, TiredError *error)
{
    if (controller->artifacts == NULL)
        return false;
    for (size_t i = 1; i <= 5; ++i)
    {
        char ledger[32];
        (void)snprintf(ledger, sizeof(ledger), "stage-%zu.json", i);
        TiredText bytes = {0};
        if (!tired_private_file_read(controller->transaction, ledger, 8192, &bytes, error))
        {
            if (error->system_errno == ENOENT)
            {
                tired_error_clear(error);
                continue;
            }
            return false;
        }
        struct json_object *document = NULL, *role = NULL, *name = NULL, *revision = NULL,
                           *uuid = NULL, *identity = NULL, *before = NULL;
        TiredFileTarget target = {0};
        TiredFileFingerprint allocated = {0}, expected = {0}, current = {0};
        TiredResolvedFile resolved = {0};
        TiredDirectory *directory = NULL;
        uint64_t value;
        bool ok = tired_json_parse(bytes.data, bytes.length, 8192, &document, error) &&
                  json_object_is_type(document, json_type_object) &&
                  json_object_object_length(document) == 6 &&
                  json_object_object_get_ex(document, "role", &role) &&
                  tired_json_u64(role, 0, 2, &value, error) &&
                  json_object_object_get_ex(document, "unit_name", &name) &&
                  json_object_is_type(name, json_type_string) &&
                  json_object_object_get_ex(document, "revision_uuid", &revision) &&
                  json_object_is_type(revision, json_type_string) &&
                  json_object_object_get_ex(document, "staging_uuid", &uuid) &&
                  json_object_is_type(uuid, json_type_string) &&
                  tired_uuid_valid(json_object_get_string(uuid),
                                   (size_t)json_object_get_string_len(uuid)) &&
                  json_object_object_get_ex(document, "identity", &identity) &&
                  json_object_object_get_ex(document, "before", &before);
        if (ok)
        {
            target.role = (TiredFileTargetRole)value;
            memcpy(target.service_uuid, controller->manifest.prepared.service_uuid, 37);
            size_t length = (size_t)json_object_get_string_len(revision);
            ok = length == 0 ||
                 (length == 36 && tired_uuid_valid(json_object_get_string(revision), length));
            if (ok && length != 0)
                memcpy(target.revision_uuid, json_object_get_string(revision), 37);
            if (ok && json_object_get_string_len(name) != 0)
                ok = tired_text_set(&target.unit_name, json_object_get_string(name),
                                    (size_t)json_object_get_string_len(name), 255, error);
            const char *a = json_object_to_json_string_ext(identity, JSON_C_TO_STRING_PLAIN);
            const char *b = json_object_to_json_string_ext(before, JSON_C_TO_STRING_PLAIN);
            ok = ok && a != NULL && b != NULL &&
                 tired_file_fingerprint_parse(a, strlen(a), &allocated, error) &&
                 allocated.exists && tired_file_fingerprint_parse(b, strlen(b), &expected, error) &&
                 tired_file_target_resolve(controller->layout, &target, &resolved, error) &&
                 tired_directory_open(resolved.directory.data, geteuid(),
                                      resolved.private_directory, &directory, error) &&
                 tired_file_fingerprint(directory, resolved.name.data, TIRED_PRIVATE_FILE_LIMIT,
                                        &current, error) &&
                 tired_file_fingerprint_equal(&current, &expected);
        }
        if (ok)
        {
            char temporary[64];
            (void)snprintf(temporary, sizeof(temporary), ".tired-%s.tmp",
                           json_object_get_string(uuid));
            struct stat actual;
            int fd = tired_directory_fd(directory);
            if (fstatat(fd, temporary, &actual, AT_SYMLINK_NOFOLLOW) != 0)
                ok = errno == ENOENT;
            else
            {
                ok = S_ISREG(actual.st_mode) && actual.st_nlink == 1 &&
                     actual.st_uid == allocated.uid && actual.st_gid == allocated.gid &&
                     actual.st_dev == allocated.device && actual.st_ino == allocated.inode &&
                     ((actual.st_mode & 07777) == 0600 ||
                      (actual.st_mode & 07777) == resolved.mode) &&
                     actual.st_size >= 0 && (uint64_t)actual.st_size <= TIRED_PRIVATE_FILE_LIMIT &&
                     tired_directory_check(directory, error);
                if (ok)
                    ok = unlinkat(fd, temporary, 0) == 0 && fsync(fd) == 0;
            }
        }
        json_object_put(document);
        tired_text_destroy(&bytes);
        tired_text_destroy(&target.unit_name);
        tired_resolved_file_destroy(&resolved);
        tired_directory_destroy(directory);
        if (!ok)
            return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "preparation-conflict",
                                   "Preparation cleanup found an altered destination or staging "
                                   "identity; no foreign file was removed.",
                                   errno);
    }
    return tired_backup_ledger_clear(controller->transaction, controller->artifacts, error) &&
           append(controller, TIRED_ACTION_ROLLBACK, TIRED_ACTION_INTENT, error) &&
           append(controller, TIRED_ACTION_ROLLBACK, TIRED_ACTION_COMPLETED, error);
}
static bool prepare(Controller *controller, const TiredMutation *mutation, TiredError *error)
{
    const TiredServiceRecord *record = &mutation->proposed;
    TiredTransactionRecord *anchor = &controller->manifest.prepared;
    memcpy(anchor->transaction_uuid, record->metadata.transaction_uuid, 37);
    memcpy(anchor->service_uuid, record->metadata.service_uuid, 37);
    memcpy(anchor->approved_sha256, record->review.approved_sha256, 65);
    anchor->operation = mutation->operation;
    anchor->action = TIRED_ACTION_PREPARE;
    anchor->state = TIRED_ACTION_COMPLETED;
    anchor->sequence = 1;
    anchor->user_scope = controller->layout->user_scope;
    controller->manifest.files = controller->files;
    if (!tired_text_set(&anchor->unit_name, record->metadata.unit_name.data,
                        record->metadata.unit_name.length, 255, error) ||
        !tired_directory_ensure(controller->layout->paths[TIRED_PATH_TRANSACTIONS].data, true,
                                &controller->transactions, error) ||
        !tired_directory_child(controller->transactions, anchor->transaction_uuid, true, true,
                               &controller->transaction, error) ||
        !tired_directory_child(controller->transaction, "journal", true, true, &controller->journal,
                               error))
        return false;
    if (!append(controller, TIRED_ACTION_PREPARE, TIRED_ACTION_COMPLETED, error))
        return false;
    controller->accepted = true;
    if (!tired_directory_child(controller->transaction, "artifacts", true, true,
                               &controller->artifacts, error))
        return false;
    TiredText bytes = {0}, unit = {0}, environment = {0};
    TiredMutation persisted = *mutation;
    persisted.proposed.environment_loaded = false;
    bool ok = tired_mutation_encode(&persisted, &bytes, error) &&
              tired_private_file_create(controller->transaction, "request.json", bytes.data,
                                        bytes.length, error);
    if (!ok)
        goto done;
    char before[128];
    int before_length = snprintf(before, sizeof(before), "{\"active\":%s,\"enabled\":%s}",
                                 controller->prior.active ? "true" : "false",
                                 controller->prior.enabled ? "true" : "false");
    if (before_length < 0 || (size_t)before_length >= sizeof(before) ||
        !tired_private_file_create(controller->transaction, "before.json", before,
                                   (size_t)before_length, error))
    {
        ok = false;
        goto done;
    }
    if (mutation->operation <= TIRED_TRANSACTION_RESTORE)
    {
        if (mutation->operation != TIRED_TRANSACTION_REMOVE)
        {
            if (record->has_environment &&
                (!tired_environment_encode(&record->environment, &environment, error) ||
                 !prepare_file(controller, TIRED_FILE_TARGET_ENVIRONMENT, NULL,
                               record->environment_revision, &environment, error)))
                goto done;
            if (!tired_render_unit(&record->spec, record->metadata.service_uuid,
                                   record->has_environment ? &record->environment_path : NULL,
                                   &record->credentials, &unit, error) ||
                !prepare_file(controller, TIRED_FILE_TARGET_UNIT, record->metadata.unit_name.data,
                              NULL, &unit, error))
                goto done;
            if (mutation->operation == TIRED_TRANSACTION_RENAME &&
                !prepare_file(controller, TIRED_FILE_TARGET_UNIT,
                              controller->old.metadata.unit_name.data, NULL, NULL, error))
                goto done;
        }
        else
        {
            if (!prepare_file(controller, TIRED_FILE_TARGET_UNIT, record->metadata.unit_name.data,
                              NULL, NULL, error))
                goto done;
            if (controller->old.has_environment && !mutation->keep_history &&
                !prepare_file(controller, TIRED_FILE_TARGET_ENVIRONMENT, NULL,
                              controller->old.environment_revision, NULL, error))
                goto done;
        }
        if (mutation->operation != TIRED_TRANSACTION_REMOVE &&
            !tired_service_record_encode(record, &bytes, error))
            goto done;
        if (!prepare_file(controller, TIRED_FILE_TARGET_RECORD, NULL, NULL,
                          mutation->operation == TIRED_TRANSACTION_REMOVE ? NULL : &bytes, error))
            goto done;
    }
    if (mutation->operation > TIRED_TRANSACTION_RESTORE)
    {
        if (!tired_service_record_encode(record, &bytes, error) ||
            !prepare_file(controller, TIRED_FILE_TARGET_RECORD, NULL, NULL, &bytes, error))
            goto done;
    }
    ok = tired_file_manifest_encode(&controller->manifest, &bytes, error) &&
         tired_private_file_create(controller->transaction, "files.json", bytes.data, bytes.length,
                                   error);
    controller->prepared = ok;
    if (ok && controller->backend->tick != NULL)
        controller->backend->tick(controller->backend->context, "prepared");
done:
    tired_text_destroy(&bytes);
    tired_text_destroy(&unit);
    tired_text_destroy(&environment);
    return ok && controller->prepared;
}
bool tired_mutation_apply(const TiredMutation *approved, const TiredLayout *layout,
                          const TiredBackend *backend, TiredOperationResult *output,
                          TiredError *error)
{
    Controller controller = {.layout = layout, .backend = backend};
    TiredMutation adjusted = {0};
    const TiredMutation *mutation = approved;
    TiredOperationResult result = {0};
    const TiredServiceRecord *record = &mutation->proposed;
    TiredRiskReport risks = {0};
    bool valid = false;
    const TiredText *lock_path = &layout->paths[TIRED_PATH_OPERATION_LOCK];
    const char *slash = strrchr(lock_path->data, '/');
    TiredText runtime = {0};
    if (slash == NULL ||
        !tired_text_set(&runtime, lock_path->data, (size_t)(slash - lock_path->data), 4096, error))
        goto done;
    bool locked = false;
    if (tired_directory_ensure(runtime.data, true, &controller.runtime_directory, error))
    {
        bool automatic = mutation->operation == TIRED_TRANSACTION_CREATE &&
                         !record->metadata.interactive &&
                         record->spec.fields[TIRED_FIELD_NAME].origin == TIRED_ORIGIN_CAPTURE;
        uint64_t deadline = tired_monotonic_usec() + UINT64_C(30000000);
        bool announced = false;
        for (;;)
        {
            locked =
                tired_operation_lock_acquire(controller.runtime_directory, &controller.lock, error);
            if (locked || !automatic || error->code == NULL ||
                strcmp(error->code, "operation-in-progress") != 0 ||
                tired_monotonic_usec() >= deadline)
                break;
            if (!announced && backend->tick != NULL)
                backend->tick(backend->context, "waiting_for_scope_lock");
            announced = true;
            struct timespec pause = {.tv_nsec = 100000000};
            while (nanosleep(&pause, &pause) != 0 && errno == EINTR)
                ;
        }
    }
    tired_text_destroy(&runtime);
    if (!locked)
        goto done;
    bool checked = tired_mutation_validate(mutation, layout, backend, &risks, error);
    if (!checked && error->code != NULL && strcmp(error->code, "name-collision") == 0 &&
        mutation->operation == TIRED_TRANSACTION_CREATE && !record->metadata.interactive &&
        record->spec.fields[TIRED_FIELD_NAME].origin == TIRED_ORIGIN_CAPTURE)
    {
        TiredText bytes = {0};
        checked = tired_mutation_encode(mutation, &bytes, error) &&
                  tired_mutation_parse(bytes.data, bytes.length, &adjusted, error) &&
                  tired_mutation_final_name(&adjusted, layout, backend, error) &&
                  tired_mutation_validate(&adjusted, layout, backend, &risks, error);
        tired_text_destroy(&bytes);
        if (checked)
        {
            mutation = &adjusted;
            record = &mutation->proposed;
        }
    }
    if (!checked)
        goto done;
    for (size_t i = 0; i < TIRED_RISK_COUNT; ++i)
        if (risks.pending[i] || (risks.present[i] && !record->review.acknowledged[i]))
        {
            tired_error_set(
                error, TIRED_INVALID, "risk-acknowledgment",
                "Review and acknowledge each required risk before applying this operation.", 0);
            goto done;
        }
    if (mutation->operation != TIRED_TRANSACTION_CREATE)
    {
        if (!tired_service_record_load(layout, record->metadata.service_uuid, &controller.old,
                                       error) ||
            !backend->query(backend->context, controller.old.metadata.unit_name.data, true,
                            &controller.prior, error))
            goto done;
    }
    if (!tired_text_set(&result.unit_name, record->metadata.unit_name.data,
                        record->metadata.unit_name.length, 255, error) ||
        !tired_text_set(&result.unit_path, record->unit_path.data, record->unit_path.length, 4096,
                        error) ||
        !tired_text_set(&result.run_as, record->spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                        record->spec.fields[TIRED_FIELD_RUN_AS].value.text.length, 255, error))
        goto done;
    result.leaves_children =
        tired_spec_choice_is(&record->spec, TIRED_FIELD_KILL_MODE, "process") ||
        tired_spec_choice_is(&record->spec, TIRED_FIELD_KILL_MODE, "none");
    result.user_scope = layout->user_scope;
    memcpy(result.service_uuid, record->metadata.service_uuid, 37);
    memcpy(result.transaction_uuid, record->metadata.transaction_uuid, 37);
    valid = true;
    if (!prepare(&controller, mutation, &result.error))
        goto failed;
    if (backend->effects != NULL)
        backend->effects(backend->context, effect, &controller);
    if (record->spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean)
    {
        bool uncertain = false;
        if (backend->linger == NULL ||
            !append(&controller, TIRED_ACTION_LINGER, TIRED_ACTION_INTENT, &result.error))
            goto failed;
        bool enabled = backend->linger(backend->context, record->metadata.invoking_uid, &uncertain,
                                       &result.error);
        if (!finish_action(&controller, TIRED_ACTION_LINGER, enabled, uncertain, &result.error))
            goto failed;
    }
    const char *name = record->metadata.unit_name.data;
    bool requested_enable = record->spec.fields[TIRED_FIELD_ENABLE].value.boolean;
    bool requested_start = record->spec.fields[TIRED_FIELD_START].value.boolean;
    if (mutation->operation <= TIRED_TRANSACTION_RESTORE)
    {
        if (!save_history(&controller, &result.error))
            goto failed;
        result.drift_backup = mutation->restore_drift;
        if ((mutation->operation == TIRED_TRANSACTION_EDIT ||
             mutation->operation == TIRED_TRANSACTION_RESTORE) &&
            !mutation->defer && !requested_start && controller.prior.active &&
            !job(&controller, name, TIRED_JOB_STOP, &result.error))
            goto failed;
        if (mutation->operation == TIRED_TRANSACTION_REMOVE ||
            mutation->operation == TIRED_TRANSACTION_RENAME)
        {
            const char *old_name = controller.old.metadata.unit_name.data;
            if (!job(&controller, old_name, TIRED_JOB_STOP, &result.error))
                goto failed;
            TiredRuntime stopped = {0};
            bool stopped_ok =
                backend->query(backend->context, old_name, true, &stopped, &result.error) &&
                !stopped.active && !stopped.running;
            bool failed = stopped.failed;
            tired_runtime_destroy(&stopped);
            if (!stopped_ok || (failed && !reset_failure(&controller, old_name, &result.error)) ||
                !enable(&controller, old_name, false, &result.error))
                goto failed;
        }
        if (!file_phase(&controller, false, false, &result.error) ||
            !reload(&controller, &result.error))
            goto failed;
        if (mutation->operation != TIRED_TRANSACTION_REMOVE)
        {
            TiredRuntime effective = {0};
            bool matched =
                backend->query(backend->context, name, true, &effective, &result.error) &&
                effective.fragment.data != NULL &&
                strcmp(effective.fragment.data, record->unit_path.data) == 0 &&
                effective.drop_ins.count == 0;
            tired_runtime_destroy(&effective);
            if (!matched)
            {
                if (result.error.status == TIRED_OK)
                    tired_error_set(
                        &result.error, TIRED_CONFLICT, "effective-config-changed",
                        "The manager loaded a different fragment or additional drop-ins.", 0);
                goto failed;
            }
        }
        if (mutation->operation != TIRED_TRANSACTION_REMOVE &&
            !enable(&controller, name, requested_enable, &result.error))
            goto failed;
        bool run = mutation->operation != TIRED_TRANSACTION_REMOVE &&
                   (mutation->operation == TIRED_TRANSACTION_CREATE ? requested_start
                    : mutation->operation == TIRED_TRANSACTION_RENAME
                        ? controller.prior.active
                        : !mutation->defer && requested_start);
        if (run)
        {
            bool started = job(&controller, name,
                               mutation->operation == TIRED_TRANSACTION_EDIT ||
                                       mutation->operation == TIRED_TRANSACTION_RESTORE
                                   ? TIRED_JOB_RESTART
                                   : TIRED_JOB_START,
                               &result.error);
            if (pending_uncertain(&controller))
                goto failed;
            if (!started ||
                !observe(&controller, mutation, name, true, &result.runtime, &result.error))
            {
                if (mutation->operation == TIRED_TRANSACTION_CREATE && controller.executed &&
                    !pending_uncertain(&controller))
                {
                    result.original_error = result.error;
                    result.status = TIRED_RUNTIME_FAILED;
                    if (!file_phase(&controller, true, false, &result.error))
                        goto failed;
                    goto commit;
                }
                goto failed;
            }
        }
        else if (mutation->operation != TIRED_TRANSACTION_REMOVE &&
                 !observe(&controller, mutation, name, false, &result.runtime, &result.error))
            goto failed;
        if (mutation->operation == TIRED_TRANSACTION_RENAME &&
            (!retire_old(&controller, &result.error) || !reload(&controller, &result.error)))
            goto failed;
        if (!file_phase(&controller, true, false, &result.error))
            goto failed;
        result.deferred = mutation->defer && controller.prior.active;
    }
    else
    {
        bool ok = true;
        if (mutation->operation == TIRED_TRANSACTION_ENABLE ||
            mutation->operation == TIRED_TRANSACTION_DISABLE)
        {
            bool enabling = mutation->operation == TIRED_TRANSACTION_ENABLE;
            ok = enable(&controller, name, enabling, &result.error);
            if (ok && mutation->now)
                ok = job(&controller, name, enabling ? TIRED_JOB_START : TIRED_JOB_STOP,
                         &result.error);
        }
        else
            ok = job(&controller, name,
                     mutation->operation == TIRED_TRANSACTION_START  ? TIRED_JOB_START
                     : mutation->operation == TIRED_TRANSACTION_STOP ? TIRED_JOB_STOP
                                                                     : TIRED_JOB_RESTART,
                     &result.error);
        if (!ok)
        {
            if (pending_uncertain(&controller))
                goto failed;
            result.original_error = result.error;
            if (!backend->query(backend->context, name, true, &result.runtime, &result.error))
                goto failed;
            result.status = TIRED_RUNTIME_FAILED;
            goto commit;
        }
        bool started = mutation->operation == TIRED_TRANSACTION_START ||
                       mutation->operation == TIRED_TRANSACTION_RESTART ||
                       (mutation->operation == TIRED_TRANSACTION_ENABLE && mutation->now);
        if (!observe(&controller, mutation, name, started, &result.runtime, &result.error))
        {
            if (result.error.status != TIRED_RUNTIME_FAILED || pending_uncertain(&controller))
                goto failed;
            result.original_error = result.error;
            result.status = TIRED_RUNTIME_FAILED;
        }
        bool stopped = mutation->operation == TIRED_TRANSACTION_STOP ||
                       (mutation->operation == TIRED_TRANSACTION_DISABLE && mutation->now);
        bool enablement = mutation->operation == TIRED_TRANSACTION_ENABLE ||
                          mutation->operation == TIRED_TRANSACTION_DISABLE;
        if ((stopped && (result.runtime.active || result.runtime.running)) ||
            (enablement &&
             result.runtime.enabled != (mutation->operation == TIRED_TRANSACTION_ENABLE)))
        {
            tired_error_set(&result.original_error, TIRED_RUNTIME_FAILED,
                            "requested-state-not-observed",
                            "The manager completed the action, but the requested final state was "
                            "not observed. Inspect status and logs.",
                            0);
            result.status = TIRED_RUNTIME_FAILED;
        }
    }
commit:
    if (mutation->operation > TIRED_TRANSACTION_RESTORE &&
        !file_phase(&controller, true, false, &result.error))
        goto failed;
    if (!tired_transaction_cleanup_mark(controller.transaction, &result.error) ||
        !append(&controller, TIRED_ACTION_COMMIT, TIRED_ACTION_INTENT, &result.error) ||
        !append(&controller, TIRED_ACTION_COMMIT, TIRED_ACTION_COMPLETED, &result.error))
        goto failed;
    if (!cleanup_files(&controller, false, &result.error) ||
        !tired_history_finalize(layout, mutation, &result.error) ||
        !tired_transaction_retire(layout, mutation, backend, &result.error))
        goto failed;
    result.installed = mutation->operation != TIRED_TRANSACTION_REMOVE;
    result.outcome = mutation->operation == TIRED_TRANSACTION_REMOVE ? "removed"
                     : result.status == TIRED_RUNTIME_FAILED         ? "committed_runtime_failed"
                     : result.deferred                               ? "committed_deferred"
                     : result.runtime.running                        ? "committed_running"
                                                                     : "committed";
    goto done;
failed:
    result.original_error = result.error;
    if (controller.accepted && !controller.prepared && !pending_uncertain(&controller) &&
        rollback_preparation(&controller, &result.error))
    {
        result.status = TIRED_ROLLED_BACK;
        result.rolled_back = true;
        result.installed = mutation->operation != TIRED_TRANSACTION_CREATE;
        result.outcome = "preparation_rolled_back";
    }
    else if (controller.prepared && !pending_uncertain(&controller) &&
             mutation->operation <= TIRED_TRANSACTION_RESTORE &&
             !(mutation->operation == TIRED_TRANSACTION_CREATE && controller.executed) &&
             rollback(&controller, mutation, false, &result.error))
    {
        result.rolled_back = true;
        result.installed = mutation->operation != TIRED_TRANSACTION_CREATE;
        result.status = TIRED_ROLLED_BACK;
        result.outcome = "rolled_back";
        if (!cleanup_files(&controller, true, &result.error) ||
            !tired_transaction_cleanup_clear(controller.transaction, &result.error))
        {
            result.recovery_required = true;
            result.status = TIRED_RECOVERY_REQUIRED;
            result.outcome = "recovery_required";
        }
    }
    else
    {
        result.recovery_required = true;
        result.status = TIRED_RECOVERY_REQUIRED;
        result.outcome = "recovery_required";
    }
done:
    if (result.rolled_back && controller.old.metadata.unit_name.data != NULL)
    {
        const TiredServiceRecord *restored = &controller.old;
        result.leaves_children =
            tired_spec_choice_is(&restored->spec, TIRED_FIELD_KILL_MODE, "process") ||
            tired_spec_choice_is(&restored->spec, TIRED_FIELD_KILL_MODE, "none");
        TiredError observation = {0};
        tired_runtime_destroy(&result.runtime);
        (void)backend->query(backend->context, restored->metadata.unit_name.data, true,
                             &result.runtime, &observation);
        if (!tired_text_set(&result.unit_name, restored->metadata.unit_name.data,
                            restored->metadata.unit_name.length, 255, &result.error) ||
            !tired_text_set(&result.unit_path, restored->unit_path.data, restored->unit_path.length,
                            4096, &result.error) ||
            !tired_text_set(&result.run_as,
                            restored->spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                            restored->spec.fields[TIRED_FIELD_RUN_AS].value.text.length,
                            TIRED_INPUT_LIMIT, &result.error))
        {
            result.status = TIRED_INTERNAL;
            result.outcome = "result-unavailable";
        }
    }
    if (backend->effects != NULL)
        backend->effects(backend->context, NULL, NULL);
    destroy(&controller);
    tired_mutation_destroy(&adjusted);
    if (valid)
    {
        tired_operation_result_destroy(output);
        *output = result;
        result = (TiredOperationResult){0};
        tired_error_clear(error);
    }
    tired_operation_result_destroy(&result);
    return valid;
}

/* The live state of an already running unit cannot prove that a pending
 * activation was submitted. Native evidence is bound to the pending intent's
 * sequence and exact returned job; an accepted job without its completion stays
 * uncertain, even when the old process is still running. */
static bool completed_job(Controller *controller, const TiredTransactionJournal *journal,
                          const char *unit, bool *succeeded, TiredError *error)
{
    size_t sequence = journal->count;
    while (sequence > 0 && journal->records[sequence - 1].state != TIRED_ACTION_INTENT)
        --sequence;
    uint64_t accepted_id = 0;
    bool completed = false;
    for (unsigned i = 1; i <= 2; ++i)
    {
        char name[32];
        (void)snprintf(name, sizeof(name), "effect-%04zu-%u.json", sequence, i);
        TiredText bytes = {0};
        if (!tired_private_file_read(controller->transaction, name, 4096, &bytes, error))
        {
            if (error->system_errno == ENOENT)
            {
                tired_error_clear(error);
                continue;
            }
            return false;
        }
        struct json_object *document = NULL, *event = NULL, *details = NULL, *recorded_unit = NULL,
                           *id = NULL, *submitted = NULL, *accepted = NULL, *finished = NULL,
                           *rejected = NULL, *result = NULL;
        uint64_t job_id = 0;
        bool valid = tired_json_parse(bytes.data, bytes.length, 4096, &document, error) &&
                     json_object_is_type(document, json_type_object) &&
                     json_object_object_length(document) == 2 &&
                     json_object_object_get_ex(document, "event_type", &event) &&
                     json_object_is_type(event, json_type_string) &&
                     json_object_object_get_ex(document, "details", &details) &&
                     json_object_is_type(details, json_type_object) &&
                     json_object_object_length(details) == 7 &&
                     json_object_object_get_ex(details, "unit", &recorded_unit) &&
                     json_object_is_type(recorded_unit, json_type_string) &&
                     (size_t)json_object_get_string_len(recorded_unit) == strlen(unit) &&
                     strcmp(json_object_get_string(recorded_unit), unit) == 0 &&
                     json_object_object_get_ex(details, "job_id", &id) &&
                     tired_json_u64(id, 1, UINT32_MAX, &job_id, error) &&
                     json_object_object_get_ex(details, "submitted", &submitted) &&
                     json_object_is_type(submitted, json_type_boolean) &&
                     json_object_get_boolean(submitted) &&
                     json_object_object_get_ex(details, "accepted", &accepted) &&
                     json_object_is_type(accepted, json_type_boolean) &&
                     json_object_get_boolean(accepted) &&
                     json_object_object_get_ex(details, "finished", &finished) &&
                     json_object_is_type(finished, json_type_boolean) &&
                     json_object_object_get_ex(details, "rejected", &rejected) &&
                     json_object_is_type(rejected, json_type_boolean) &&
                     !json_object_get_boolean(rejected) &&
                     json_object_object_get_ex(details, "result", &result) &&
                     json_object_is_type(result, json_type_string) &&
                     (!accepted_id || accepted_id == job_id) && !completed;
        if (valid)
        {
            bool done = json_object_get_boolean(finished);
            valid = strcmp(json_object_get_string(event), done ? "job_finished" : "job_accepted") ==
                        0 &&
                    (size_t)json_object_get_string_len(event) ==
                        strlen(done ? "job_finished" : "job_accepted") &&
                    (done ? json_object_get_string_len(result) > 0
                          : json_object_get_string_len(result) == 0);
            if (valid)
            {
                accepted_id = job_id;
                completed = done;
                *succeeded = done && json_object_get_string_len(result) == 4 &&
                             strcmp(json_object_get_string(result), "done") == 0;
            }
        }
        json_object_put(document);
        tired_text_destroy(&bytes);
        if (!valid)
            return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "runtime-evidence-invalid",
                                   "Stored runtime job evidence does not match the pending action. "
                                   "The action will not be repeated.",
                                   0);
    }
    return completed || tired_error_set(error, TIRED_RECOVERY_REQUIRED, "uncertain-runtime-action",
                                        "The requested activation has no recorded job completion. "
                                        "A running process cannot prove it occurred; the action "
                                        "will not be repeated.",
                                        0);
}
static bool action_failed(const TiredTransactionJournal *journal, TiredTransactionAction action)
{
    for (size_t i = journal->count; i > 0; --i)
        if (journal->records[i - 1].action == action &&
            (journal->records[i - 1].state == TIRED_ACTION_COMPLETED ||
             journal->records[i - 1].state == TIRED_ACTION_FAILED))
            return journal->records[i - 1].state == TIRED_ACTION_FAILED;
    return false;
}
static bool recover_pending(Controller *controller, const TiredMutation *mutation,
                            const TiredTransactionJournal *journal, TiredError *error)
{
    if (!journal->progress.pending)
        return true;
    TiredTransactionAction action = journal->progress.pending_action;
    if (action == TIRED_ACTION_START || action == TIRED_ACTION_STOP ||
        action == TIRED_ACTION_RESTART)
    {
        TiredRuntime state = {0};
        const char *unit = mutation->proposed.metadata.unit_name.data;
        if (journal->progress.mode == TIRED_PROGRESS_ROLLBACK && action == TIRED_ACTION_START &&
            controller->old.metadata.unit_name.data != NULL)
            unit = controller->old.metadata.unit_name.data;
        else if (journal->progress.mode != TIRED_PROGRESS_ROLLBACK && action == TIRED_ACTION_STOP &&
                 mutation->previous_name.data != NULL && mutation->previous_name.length != 0)
            unit = mutation->previous_name.data;
        bool succeeded = true;
        if (action != TIRED_ACTION_STOP &&
            !completed_job(controller, journal, unit, &succeeded, error))
            return false;
        bool read =
            controller->backend->query(controller->backend->context, unit, true, &state, error);
        bool established =
            read && state.job_known && state.job_id == 0 &&
            (state.running || state.completed || state.failed ||
             (state.active_state.data != NULL && strcmp(state.active_state.data, "inactive") == 0));
        bool achieved = established &&
                        (action == TIRED_ACTION_STOP ? !state.active && !state.running : succeeded);
        tired_runtime_destroy(&state);
        if (!established)
            return tired_error_set(
                error, TIRED_RECOVERY_REQUIRED, "uncertain-runtime-action",
                "The prior runtime job has not been reconciled. It will not be repeated. Inspect "
                "status and retry recovery after its outcome is known.",
                0);
        return append(controller, action, achieved ? TIRED_ACTION_COMPLETED : TIRED_ACTION_FAILED,
                      error);
    }
    else if (action == TIRED_ACTION_COMMIT)
    {
        /* A commit intent is reached only after installation/runtime evidence.
         * Reconciliation still verifies every destination before marking complete. */
        TiredFileReconciliation files = {0};
        bool after =
            tired_file_reconcile(controller->layout, &controller->manifest, &files, error) &&
            files.complete && !files.foreign;
        for (size_t i = 0; after && i < files.count; ++i)
            after = files.files[i].state == TIRED_FILE_AFTER;
        tired_file_reconciliation_destroy(&files);
        if (!after)
            return false;
    }
    /* File intent is reconciled by the manifest on the next explicit step.
     * Reload/enablement are idempotent and may be requested again. A failed
     * historical outcome closes the old uncertain action without asserting that
     * no effect occurred; this is distinct from runtime job replay. */
    return append(controller, action,
                  action == TIRED_ACTION_START || action == TIRED_ACTION_STOP ||
                          action == TIRED_ACTION_RESTART || action == TIRED_ACTION_COMMIT
                      ? TIRED_ACTION_COMPLETED
                      : TIRED_ACTION_FAILED,
                  error);
}
/* Before the first anchor/request pair, no destination staging or manager
 * action is allowed. Inspect the entire private bootstrap before retiring it. */
static bool recover_bootstrap(Controller *controller, const TiredTransactionJournal *journal,
                              const char *uuid, bool finish, TiredOperationResult *result,
                              TiredError *error)
{
    if (finish)
        return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "bootstrap-rollback-required",
                               "Preparation did not finish. Select rollback, then review again.",
                               0);
    if (journal->count > 1 || journal->staging_count != 0)
        return tired_error_set(error, TIRED_CONFLICT, "bootstrap-journal",
                               "This is not an unpublished preparation.", 0);
    int scan_fd = openat(tired_directory_fd(controller->transaction), ".",
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    DIR *scan = scan_fd < 0 ? NULL : fdopendir(scan_fd);
    if (scan == NULL)
    {
        if (scan_fd >= 0)
            (void)close(scan_fd);
        return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "bootstrap-read",
                               "Cannot inspect incomplete preparation.", errno);
    }
    TiredFileFingerprint request = {0}, before = {0};
    bool ok = true;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (entry == NULL)
        {
            ok = errno == 0;
            break;
        }
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "journal") == 0)
            continue;
        if (strcmp(name, "artifacts") == 0)
        {
            TiredDirectory *artifacts = NULL;
            ok = tired_directory_child(controller->transaction, name, false, true, &artifacts,
                                       error);
            /* The only bootstrap artifact directory is empty. */
            int fd = ok ? openat(tired_directory_fd(artifacts), ".",
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
                        : -1;
            DIR *children = fd < 0 ? NULL : fdopendir(fd);
            ok = ok && children != NULL;
            if (children == NULL && fd >= 0)
                (void)close(fd);
            struct dirent *child;
            while (ok)
            {
                errno = 0;
                child = readdir(children);
                if (child == NULL)
                {
                    ok = errno == 0;
                    break;
                }
                ok = strcmp(child->d_name, ".") == 0 || strcmp(child->d_name, "..") == 0;
            }
            if (children != NULL)
                ok = closedir(children) == 0 && ok;
            tired_directory_destroy(artifacts);
        }
        else if (strcmp(name, "request.json") == 0 || strcmp(name, "before.json") == 0)
        {
            TiredFileFingerprint *expected = strcmp(name, "request.json") == 0 ? &request : &before;
            ok = tired_file_fingerprint(controller->transaction, name, TIRED_INPUT_LIMIT, expected,
                                        error) &&
                 expected->exists && expected->uid == geteuid() && expected->mode == 0600;
        }
        else
            ok = false;
        if (!ok)
            break;
    }
    ok = closedir(scan) == 0 && ok;
    if (!ok)
        return tired_error_set(
            error, TIRED_CONFLICT, "bootstrap-foreign",
            "Incomplete preparation contains unexpected files; none were removed.", 0);
    if (journal->count == 1)
    {
        TiredDirectory *artifacts = NULL;
        if (!tired_directory_child(controller->transaction, "artifacts", true, true, &artifacts,
                                   error))
            return false;
        tired_directory_destroy(artifacts);
        const TiredTransactionRecord *anchor = &journal->records[0];
        if (strcmp(anchor->transaction_uuid, uuid) != 0 ||
            anchor->user_scope != controller->layout->user_scope)
            return tired_error_set(error, TIRED_CONFLICT, "bootstrap-binding",
                                   "Preparation identity does not match its directory.", 0);
        if (!tired_text_set(&controller->manifest.prepared.unit_name, anchor->unit_name.data,
                            anchor->unit_name.length, 255, error))
            return false;
        TiredText unit = controller->manifest.prepared.unit_name;
        controller->manifest.prepared = *anchor;
        controller->manifest.prepared.unit_name = unit;
        if (!append(controller, TIRED_ACTION_ROLLBACK, TIRED_ACTION_INTENT, error) ||
            !append(controller, TIRED_ACTION_ROLLBACK, TIRED_ACTION_COMPLETED, error))
            return false;
        memcpy(result->service_uuid, anchor->service_uuid, 37);
        result->installed = anchor->operation != TIRED_TRANSACTION_CREATE;
        if (!tired_text_set(&result->unit_name, anchor->unit_name.data, anchor->unit_name.length,
                            255, error))
            return false;
    }
    if (!cleanup_entry(controller->transaction, "request.json", &request, error) ||
        !cleanup_entry(controller->transaction, "before.json", &before, error))
        return false;
    if (journal->count == 0)
    {
        int fd = tired_directory_fd(controller->transaction);
        const char *children[] = {"artifacts", "journal"};
        for (size_t i = 0; i < 2; ++i)
            if (unlinkat(fd, children[i], AT_REMOVEDIR) != 0 && errno != ENOENT)
                return false;
        if (!tired_directory_check(controller->transaction, error) ||
            unlinkat(tired_directory_fd(controller->transactions), uuid, AT_REMOVEDIR) != 0 ||
            fsync(tired_directory_fd(controller->transactions)) != 0)
            return false;
    }
    memcpy(result->transaction_uuid, uuid, 37);
    result->user_scope = controller->layout->user_scope;
    result->rolled_back = true;
    result->outcome = "preparation_rolled_back";
    return true;
}
bool tired_mutation_recover(const TiredLayout *layout, const TiredBackend *backend,
                            const char *transaction_uuid, bool finish, TiredOperationResult *output,
                            TiredError *error)
{
    if (!tired_uuid_valid(transaction_uuid, strlen(transaction_uuid)))
        return tired_error_set(error, TIRED_INVALID, "recovery-id",
                               "Expected a canonical transaction UUID.", 0);
    Controller controller = {.layout = layout, .backend = backend};
    TiredMutation mutation = {0};
    TiredFileManifest manifest = {0};
    TiredTransactionJournal journal = {0};
    TiredFileReconciliation files = {0};
    TiredOperationResult result = {0};
    TiredText bytes = {0}, runtime_path = {0};
    bool valid = false;
    const TiredText *lock = &layout->paths[TIRED_PATH_OPERATION_LOCK];
    const char *slash = strrchr(lock->data, '/');
    if (slash == NULL ||
        !tired_text_set(&runtime_path, lock->data, (size_t)(slash - lock->data), 4096, error) ||
        !tired_directory_ensure(runtime_path.data, true, &controller.runtime_directory, error) ||
        !tired_operation_lock_acquire(controller.runtime_directory, &controller.lock, error) ||
        !tired_directory_open(layout->paths[TIRED_PATH_TRANSACTIONS].data, geteuid(), true,
                              &controller.transactions, error) ||
        !tired_directory_child(controller.transactions, transaction_uuid, false, true,
                               &controller.transaction, error))
        goto done;
    bool journal_open = tired_directory_child(controller.transaction, "journal", false, true,
                                              &controller.journal, error);
    if ((!journal_open && error->status != TIRED_NOT_FOUND) ||
        (journal_open && !tired_transaction_journal_read(controller.journal, &journal, error)))
        goto done;
    if (journal.count == 0)
    {
        tired_error_clear(error);
        valid = true;
        if (!recover_bootstrap(&controller, &journal, transaction_uuid, finish, &result,
                               &result.error))
            goto failed;
        goto done;
    }
    TiredFileFingerprint cleanup = {0};
    if (!tired_file_fingerprint(controller.transaction, "cleanup.json", TIRED_PRIVATE_FILE_LIMIT,
                                &cleanup, error))
        goto done;
    if (cleanup.exists)
    {
        if (journal.count == 0 || journal.staging_count != 0 ||
            strcmp(journal.records[0].transaction_uuid, transaction_uuid) != 0 ||
            journal.records[0].user_scope != layout->user_scope ||
            journal.progress.mode != TIRED_PROGRESS_COMMITTED)
        {
            tired_error_set(error, TIRED_CONFLICT, "cleanup-binding",
                            "Cleanup ledger does not match a committed transaction.", 0);
            goto done;
        }
        const TiredTransactionRecord *anchor = &journal.records[0];
        memcpy(result.service_uuid, anchor->service_uuid, 37);
        memcpy(result.transaction_uuid, transaction_uuid, 37);
        result.user_scope = layout->user_scope;
        if (!tired_text_set(&result.unit_name, anchor->unit_name.data, anchor->unit_name.length,
                            255, error))
            goto done;
        TiredFileTarget target = {.role = TIRED_FILE_TARGET_UNIT, .unit_name = anchor->unit_name};
        memcpy(target.service_uuid, anchor->service_uuid, 37);
        TiredResolvedFile resolved = {0};
        bool derived = tired_file_target_resolve(layout, &target, &resolved, error) &&
                       tired_path_absolute(&resolved.directory, resolved.name.data,
                                           resolved.name.length, &result.unit_path, error);
        tired_resolved_file_destroy(&resolved);
        if (!derived)
            goto done;
        valid = true;
        if (!finish)
        {
            tired_error_set(&result.error, TIRED_RECOVERY_REQUIRED, "committed-cleanup-finish",
                            "The operation committed. Select finish to resume cleanup; rollback "
                            "would require a new reviewed operation.",
                            0);
            goto failed;
        }
        if (!tired_transaction_cleanup_resume(layout, controller.transaction, anchor, backend,
                                              &result.error))
            goto failed;
        result.installed = anchor->operation != TIRED_TRANSACTION_REMOVE;
        result.outcome = "committed_cleanup_finished";
        if (result.installed)
            (void)backend->query(backend->context, anchor->unit_name.data, false, &result.runtime,
                                 &result.error);
        goto done;
    }
    TiredFileFingerprint request_file = {0}, before_file = {0};
    if (!tired_file_fingerprint(controller.transaction, "request.json", TIRED_INPUT_LIMIT,
                                &request_file, error) ||
        !tired_file_fingerprint(controller.transaction, "before.json", 128, &before_file, error))
        goto done;
    if ((!request_file.exists || !before_file.exists) && journal.count == 1)
    {
        valid = true;
        if (!recover_bootstrap(&controller, &journal, transaction_uuid, finish, &result,
                               &result.error))
            goto failed;
        goto done;
    }
    if (!tired_directory_child(controller.transaction, "artifacts", false, true,
                               &controller.artifacts, error) ||
        !tired_private_file_read(controller.transaction, "request.json", TIRED_INPUT_LIMIT, &bytes,
                                 error) ||
        !tired_mutation_parse(bytes.data, bytes.length, &mutation, error))
        goto done;
    TiredFileFingerprint manifest_file = {0};
    if (!tired_file_fingerprint(controller.transaction, "files.json", TIRED_FILE_MANIFEST_LIMIT,
                                &manifest_file, error))
        goto done;
    if (!manifest_file.exists)
    {
        if (journal.count == 0 || journal.staging_count != 0 ||
            strcmp(journal.records[0].transaction_uuid, transaction_uuid) != 0 ||
            journal.records[0].user_scope != layout->user_scope ||
            strcmp(journal.records[0].approved_sha256, mutation.proposed.review.approved_sha256) !=
                0 ||
            !tired_service_record_check_layout(&mutation.proposed, layout, error) ||
            !(journal.count == 1 || journal.progress.mode == TIRED_PROGRESS_ROLLED_BACK))
        {
            tired_error_set(error, TIRED_CONFLICT, "preparation-binding",
                            "Incomplete preparation does not match its accepted request.", 0);
            goto done;
        }
        controller.manifest.prepared = journal.records[0];
        journal.records[0].unit_name = (TiredText){0};
        memcpy(result.service_uuid, mutation.proposed.metadata.service_uuid, 37);
        memcpy(result.transaction_uuid, transaction_uuid, 37);
        result.leaves_children =
            tired_spec_choice_is(&mutation.proposed.spec, TIRED_FIELD_KILL_MODE, "process") ||
            tired_spec_choice_is(&mutation.proposed.spec, TIRED_FIELD_KILL_MODE, "none");
        result.user_scope = layout->user_scope;
        if (!tired_text_set(&result.unit_name, mutation.proposed.metadata.unit_name.data,
                            mutation.proposed.metadata.unit_name.length, 255, error) ||
            !tired_text_set(&result.unit_path, mutation.proposed.unit_path.data,
                            mutation.proposed.unit_path.length, 4096, error) ||
            !tired_text_set(
                &result.run_as, mutation.proposed.spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                mutation.proposed.spec.fields[TIRED_FIELD_RUN_AS].value.text.length, 255, error))
            goto done;
        valid = true;
        if (journal.progress.mode != TIRED_PROGRESS_ROLLED_BACK && finish)
        {
            tired_error_set(&result.error, TIRED_RECOVERY_REQUIRED, "preparation-rollback-required",
                            "Preparation was interrupted before publication. Select rollback, then "
                            "review and submit the operation again.",
                            0);
            goto failed;
        }
        if (journal.progress.mode != TIRED_PROGRESS_ROLLED_BACK &&
            !rollback_preparation(&controller, &result.error))
            goto failed;
        result.rolled_back = true;
        result.installed = mutation.operation != TIRED_TRANSACTION_CREATE;
        result.outcome = "preparation_rolled_back";
        goto done;
    }
    if (!tired_manifest_load(controller.transaction, &manifest, error))
        goto done;
    if (manifest.count > 5 || journal.count == 0 || journal.staging_count != 0 ||
        manifest.prepared.user_scope != layout->user_scope ||
        strcmp(manifest.prepared.transaction_uuid, transaction_uuid) != 0 ||
        strcmp(manifest.prepared.approved_sha256, mutation.proposed.review.approved_sha256) != 0 ||
        !tired_service_record_check_layout(&mutation.proposed, layout, error))
    {
        tired_error_set(
            error, TIRED_CONFLICT, "recovery-binding",
            "Recovery files are incomplete or do not agree with the approved operation.", 0);
        goto done;
    }
    controller.manifest.prepared = manifest.prepared;
    manifest.prepared.unit_name = (TiredText){0};
    controller.manifest.count = manifest.count;
    controller.manifest.files = controller.files;
    for (size_t i = 0; i < manifest.count; ++i)
    {
        controller.files[i] = manifest.files[i];
        manifest.files[i].target.unit_name = (TiredText){0};
    }
    controller.prepared = true;
    for (size_t i = 0; i < journal.count; ++i)
        controller.executed |= journal.records[i].action == TIRED_ACTION_START ||
                               journal.records[i].action == TIRED_ACTION_RESTART;
    if (!tired_private_file_read(controller.transaction, "before.json", 128, &bytes, error))
        goto done;
    struct json_object *before = NULL, *active = NULL, *enabled = NULL;
    bool before_ok = tired_json_parse(bytes.data, bytes.length, 128, &before, error) &&
                     json_object_is_type(before, json_type_object) &&
                     json_object_object_length(before) == 2 &&
                     json_object_object_get_ex(before, "active", &active) &&
                     json_object_is_type(active, json_type_boolean) &&
                     json_object_object_get_ex(before, "enabled", &enabled) &&
                     json_object_is_type(enabled, json_type_boolean);
    if (before_ok)
    {
        controller.prior.active = json_object_get_boolean(active);
        controller.prior.enabled = json_object_get_boolean(enabled);
    }
    json_object_put(before);
    if (!before_ok)
        goto done;
    for (size_t i = 0; i < controller.manifest.count; ++i)
        if (controller.files[i].target.role == TIRED_FILE_TARGET_RECORD &&
            controller.files[i].before.exists)
        {
            if (!tired_private_file_read(controller.artifacts, controller.files[i].rollback_uuid,
                                         TIRED_SERVICE_RECORD_LIMIT, &bytes, error) ||
                !tired_service_record_parse(bytes.data, bytes.length, &controller.old, error))
                goto done;
        }
    if (!tired_file_reconcile(layout, &controller.manifest, &files, error) || !files.complete ||
        files.foreign)
    {
        tired_error_set(error, TIRED_CONFLICT, "recovery-foreign-files",
                        "Recovery found unsafe, changed or unobservable destinations. Foreign "
                        "files will not be overwritten.",
                        0);
        goto done;
    }
    const TiredServiceRecord *record = &mutation.proposed;
    mutation.root_previously_selected =
        controller.old.metadata.service_uuid[0] != '\0' && controller.old.metadata.service_uid == 0;
    memcpy(result.service_uuid, record->metadata.service_uuid, 37);
    memcpy(result.transaction_uuid, transaction_uuid, 37);
    result.leaves_children =
        tired_spec_choice_is(&record->spec, TIRED_FIELD_KILL_MODE, "process") ||
        tired_spec_choice_is(&record->spec, TIRED_FIELD_KILL_MODE, "none");
    result.user_scope = layout->user_scope;
    if (!tired_text_set(&result.unit_name, record->metadata.unit_name.data,
                        record->metadata.unit_name.length, 255, error) ||
        !tired_text_set(&result.unit_path, record->unit_path.data, record->unit_path.length, 4096,
                        error) ||
        !tired_text_set(&result.run_as, record->spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                        record->spec.fields[TIRED_FIELD_RUN_AS].value.text.length, 255, error))
        goto done;
    valid = true;
    if (journal.progress.mode == TIRED_PROGRESS_COMMITTED ||
        journal.progress.mode == TIRED_PROGRESS_ROLLED_BACK)
    {
        if (!cleanup_files(&controller, journal.progress.mode == TIRED_PROGRESS_ROLLED_BACK,
                           &result.error))
            goto failed;
        result.installed = journal.progress.mode == TIRED_PROGRESS_COMMITTED &&
                           mutation.operation != TIRED_TRANSACTION_REMOVE;
        result.rolled_back = journal.progress.mode == TIRED_PROGRESS_ROLLED_BACK;
        result.outcome = result.rolled_back ? "already_rolled_back" : "already_committed";
        goto observe_final;
    }
    if (backend->effects != NULL)
        backend->effects(backend->context, effect, &controller);
    if (!recover_pending(&controller, &mutation, &journal, &result.error))
        goto failed;
    if (journal.progress.pending && journal.progress.pending_action == TIRED_ACTION_COMMIT)
    {
        result.installed = mutation.operation != TIRED_TRANSACTION_REMOVE;
        result.outcome = "recovered_committed";
        goto observe_final;
    }
    tired_transaction_journal_destroy(&journal);
    if (!tired_transaction_journal_read(controller.journal, &journal, &result.error))
        goto failed;
    if (!finish || journal.progress.mode == TIRED_PROGRESS_ROLLBACK)
    {
        if (!rollback(&controller, &mutation, journal.progress.mode == TIRED_PROGRESS_ROLLBACK,
                      &result.error))
            goto failed;
        result.rolled_back = true;
        result.installed = mutation.operation != TIRED_TRANSACTION_CREATE;
        result.outcome = "rolled_back";
        goto observe_final;
    }
    if (record->spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean)
    {
        bool uncertain = false;
        if (backend->linger == NULL ||
            !append(&controller, TIRED_ACTION_LINGER, TIRED_ACTION_INTENT, &result.error))
            goto failed;
        bool enabled = backend->linger(backend->context, record->metadata.invoking_uid, &uncertain,
                                       &result.error);
        if (!finish_action(&controller, TIRED_ACTION_LINGER, enabled, uncertain, &result.error))
            goto failed;
    }
    if (mutation.operation <= TIRED_TRANSACTION_RESTORE)
    {
        if (!save_history(&controller, &result.error))
            goto failed;
        bool stop_requested = (mutation.operation == TIRED_TRANSACTION_EDIT ||
                               mutation.operation == TIRED_TRANSACTION_RESTORE) &&
                              !mutation.defer &&
                              !record->spec.fields[TIRED_FIELD_START].value.boolean;
        if (stop_requested)
        {
            TiredRuntime current = {0};
            bool read = backend->query(backend->context, record->metadata.unit_name.data, true,
                                       &current, &result.error);
            bool needs_stop =
                current.active || current.running || (current.job_known && current.job_id != 0);
            tired_runtime_destroy(&current);
            if (!read || (needs_stop && !job(&controller, record->metadata.unit_name.data,
                                             TIRED_JOB_STOP, &result.error)))
                goto failed;
        }
        if (mutation.operation == TIRED_TRANSACTION_REMOVE ||
            mutation.operation == TIRED_TRANSACTION_RENAME)
        {
            const char *old_name = controller.old.metadata.unit_name.data;
            if (old_name == NULL)
                goto failed;
            TiredRuntime old = {0};
            bool stopped = backend->query(backend->context, old_name, false, &old, &result.error);
            bool active_now = old.active;
            bool failed = old.failed;
            tired_runtime_destroy(&old);
            if (!stopped ||
                (active_now && !job(&controller, old_name, TIRED_JOB_STOP, &result.error)) ||
                (failed && !reset_failure(&controller, old_name, &result.error)) ||
                !enable(&controller, old_name, false, &result.error))
                goto failed;
        }
        if (!file_phase(&controller, false, false, &result.error) ||
            !reload(&controller, &result.error))
            goto failed;
        if (mutation.operation != TIRED_TRANSACTION_REMOVE)
        {
            if (!enable(&controller, record->metadata.unit_name.data,
                        record->spec.fields[TIRED_FIELD_ENABLE].value.boolean, &result.error))
                goto failed;
            bool runtime_requested =
                mutation.operation == TIRED_TRANSACTION_CREATE
                    ? record->spec.fields[TIRED_FIELD_START].value.boolean
                : mutation.operation == TIRED_TRANSACTION_RENAME
                    ? controller.prior.active
                    : record->spec.fields[TIRED_FIELD_START].value.boolean && !mutation.defer;
            bool admitted = false;
            for (size_t i = 0; i < journal.count; ++i)
                admitted |= journal.records[i].action == TIRED_ACTION_START ||
                            journal.records[i].action == TIRED_ACTION_RESTART;
            if (runtime_requested && !admitted)
            {
                TiredRiskReport current = {0};
                TiredMutation recovering = mutation;
                recovering.recovery = true;
                if (!tired_mutation_workload_check(&recovering, layout, backend, &current,
                                                   &result.error))
                    goto failed;
                for (size_t i = 0; i < TIRED_RISK_COUNT; ++i)
                    if (current.pending[i] ||
                        (current.present[i] && !record->review.acknowledged[i]))
                    {
                        tired_error_set(
                            &result.error, TIRED_RECOVERY_REQUIRED, "recovery-risk-changed",
                            "Current workload checks require a fresh review before execution.", 0);
                        goto failed;
                    }
                TiredRuntime effective = {0};
                bool matched = backend->query(backend->context, record->metadata.unit_name.data,
                                              true, &effective, &result.error) &&
                               effective.fragment.data != NULL &&
                               strcmp(effective.fragment.data, record->unit_path.data) == 0 &&
                               effective.drop_ins.count == 0;
                tired_runtime_destroy(&effective);
                if (!matched)
                    goto failed;
            }
            TiredTransactionAction activation =
                mutation.operation == TIRED_TRANSACTION_EDIT ||
                        mutation.operation == TIRED_TRANSACTION_RESTORE
                    ? TIRED_ACTION_RESTART
                    : TIRED_ACTION_START;
            bool activation_failed =
                runtime_requested && (action_failed(&journal, activation) ||
                                      action_failed(&journal, TIRED_ACTION_OBSERVE));
            if (runtime_requested && !admitted &&
                !job(&controller, record->metadata.unit_name.data,
                     activation == TIRED_ACTION_RESTART ? TIRED_JOB_RESTART : TIRED_JOB_START,
                     &result.error))
                goto failed;
            if (activation_failed)
                tired_error_set(&result.error, TIRED_RUNTIME_FAILED, "recovered-runtime-failed",
                                "The recorded activation or initial observation failed. Inspect "
                                "status and logs.",
                                0);
            if (activation_failed ||
                !observe(&controller, &mutation, record->metadata.unit_name.data, runtime_requested,
                         &result.runtime, &result.error))
            {
                if (pending_uncertain(&controller))
                    goto failed;
                result.original_error = result.error;
                if (mutation.operation != TIRED_TRANSACTION_CREATE)
                {
                    if (!rollback(&controller, &mutation, false, &result.error))
                        goto failed;
                    result.rolled_back = true;
                    result.installed = true;
                    result.status = TIRED_ROLLED_BACK;
                    result.outcome = "rolled_back";
                    goto observe_final;
                }
                result.status = TIRED_RUNTIME_FAILED;
            }
        }
        if (mutation.operation == TIRED_TRANSACTION_RENAME &&
            (!retire_old(&controller, &result.error) || !reload(&controller, &result.error)))
            goto failed;
        if (!file_phase(&controller, true, false, &result.error))
            goto failed;
    }
    else
    {
        /* Accepted runtime actions are observed, never replayed. An action whose
         * intent was never written can be admitted after current validation. */
        if (mutation.operation == TIRED_TRANSACTION_ENABLE ||
            mutation.operation == TIRED_TRANSACTION_DISABLE)
            if (!enable(&controller, record->metadata.unit_name.data,
                        mutation.operation == TIRED_TRANSACTION_ENABLE, &result.error))
                goto failed;
        TiredTransactionAction requested =
            mutation.operation == TIRED_TRANSACTION_STOP ||
                    (mutation.operation == TIRED_TRANSACTION_DISABLE && mutation.now)
                ? TIRED_ACTION_STOP
            : mutation.operation == TIRED_TRANSACTION_RESTART ? TIRED_ACTION_RESTART
                                                              : TIRED_ACTION_START;
        bool runtime_requested = mutation.operation == TIRED_TRANSACTION_START ||
                                 mutation.operation == TIRED_TRANSACTION_STOP ||
                                 mutation.operation == TIRED_TRANSACTION_RESTART || mutation.now;
        bool admitted = false;
        for (size_t i = 0; i < journal.count; ++i)
            admitted |= journal.records[i].action == requested;
        if (runtime_requested && !admitted)
        {
            TiredRiskReport risks = {0};
            mutation.recovery = true;
            bool safe =
                tired_mutation_workload_check(&mutation, layout, backend, &risks, &result.error);
            for (size_t i = 0; safe && i < TIRED_RISK_COUNT; ++i)
                safe = !risks.pending[i] && (!risks.present[i] || record->review.acknowledged[i]);
            if (!safe || !job(&controller, record->metadata.unit_name.data,
                              requested == TIRED_ACTION_STOP      ? TIRED_JOB_STOP
                              : requested == TIRED_ACTION_RESTART ? TIRED_JOB_RESTART
                                                                  : TIRED_JOB_START,
                              &result.error))
                goto failed;
        }
        bool starting = runtime_requested && requested != TIRED_ACTION_STOP;
        bool runtime_failed =
            runtime_requested && (action_failed(&journal, requested) ||
                                  (starting && action_failed(&journal, TIRED_ACTION_OBSERVE)));
        if (runtime_failed)
            tired_error_set(&result.error, TIRED_RUNTIME_FAILED, "recovered-job-failed",
                            "The recorded runtime job failed. Inspect status and logs.", 0);
        if (runtime_failed || !observe(&controller, &mutation, record->metadata.unit_name.data,
                                       starting, &result.runtime, &result.error))
        {
            if (pending_uncertain(&controller) || result.error.status != TIRED_RUNTIME_FAILED)
                goto failed;
            result.original_error = result.error;
            result.status = TIRED_RUNTIME_FAILED;
        }
    }
    if (mutation.operation > TIRED_TRANSACTION_RESTORE &&
        !file_phase(&controller, true, false, &result.error))
        goto failed;
    if (!tired_transaction_cleanup_mark(controller.transaction, &result.error) ||
        !append(&controller, TIRED_ACTION_COMMIT, TIRED_ACTION_INTENT, &result.error) ||
        !append(&controller, TIRED_ACTION_COMMIT, TIRED_ACTION_COMPLETED, &result.error))
        goto failed;
    result.installed = mutation.operation != TIRED_TRANSACTION_REMOVE;
    result.outcome =
        result.status == TIRED_RUNTIME_FAILED ? "committed_runtime_failed" : "recovered_committed";
observe_final:
    if (!cleanup_files(&controller, result.rolled_back, &result.error) ||
        (result.rolled_back &&
         !tired_transaction_cleanup_clear(controller.transaction, &result.error)) ||
        (!result.rolled_back &&
         (!tired_history_finalize(layout, &mutation, &result.error) ||
          !tired_transaction_retire(layout, &mutation, backend, &result.error))))
        goto failed;
    if (result.installed)
    {
        const TiredServiceRecord *installed = result.rolled_back ? &controller.old : record;
        if (!tired_text_set(&result.unit_name, installed->metadata.unit_name.data,
                            installed->metadata.unit_name.length, 255, &result.error) ||
            !tired_text_set(&result.unit_path, installed->unit_path.data,
                            installed->unit_path.length, 4096, &result.error))
            goto failed;
        tired_runtime_destroy(&result.runtime);
        (void)backend->query(backend->context, installed->metadata.unit_name.data, true,
                             &result.runtime, &result.error);
    }
    goto done;
failed:
    result.recovery_required = true;
    result.status = TIRED_RECOVERY_REQUIRED;
    result.outcome = "recovery_required";
done:
    tired_file_reconciliation_destroy(&files);
    tired_transaction_journal_destroy(&journal);
    tired_file_manifest_destroy(&manifest);
    tired_mutation_destroy(&mutation);
    tired_text_destroy(&bytes);
    tired_text_destroy(&runtime_path);
    if (backend->effects != NULL)
        backend->effects(backend->context, NULL, NULL);
    destroy(&controller);
    if (valid)
    {
        tired_operation_result_destroy(output);
        *output = result;
        result = (TiredOperationResult){0};
    }
    tired_operation_result_destroy(&result);
    return valid;
}
