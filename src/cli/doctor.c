#include "tired/doctor.h"
#include "tired/backend.h"
#include "tired/encode.h"
#include "tired/identity.h"
#include "tired/journal_access.h"
#include "tired/linger_observation.h"
#include "tired/mutation.h"
#include "tired/name.h"
#include "tired/service_files.h"
#include "tired/service_inventory.h"
#include "tired/service_record_storage.h"
#include "tired/transaction_inventory.h"
#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <systemd/sd-id128.h>
#include <unistd.h>
static bool put(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static bool check(struct json_object *checks, const char *id, const char *severity,
                  const char *observation, const char *correction)
{
    struct json_object *object = json_object_new_object();
    bool ok = object != NULL && put(object, "id", json_object_new_string(id)) &&
              put(object, "severity", json_object_new_string(severity)) &&
              put(object, "observation", json_object_new_string(observation)) &&
              put(object, "correction", json_object_new_string(correction));
    if (ok && json_object_array_add(checks, object) == 0)
        return true;
    json_object_put(object);
    return false;
}
static bool path_check(struct json_object *checks, const char *id, const char *path, bool directory,
                       bool executable)
{
    struct stat file;
    bool found = path != NULL && stat(path, &file) == 0 &&
                 (directory ? S_ISDIR(file.st_mode) : S_ISREG(file.st_mode)) &&
                 (!executable || (file.st_mode & 0111) != 0);
    bool volatile_path =
        path != NULL && (strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/run/", 5) == 0 ||
                         strncmp(path, "/var/tmp/", 9) == 0);
    return check(
        checks, id,
        !found          ? "error"
        : volatile_path ? "warning"
                        : "info",
        !found          ? "Selected path is unavailable or has the wrong file type."
        : volatile_path ? "Selected path is volatile/session dependent."
                        : "Selected path currently exists; this does not prove all "
                          "workload-account permissions.",
        !found
            ? "Restore the application-owned file or edit the service to select an accessible path."
        : volatile_path
            ? "Choose a persistent application location before relying on reboot activation."
            : "Recheck after moving files or changing permissions.");
}
bool tired_doctor_command(const TiredRequest *request, TiredText *output, TiredStatus *status,
                          TiredError *error)
{
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    TiredLayout layout = {0};
    TiredNativeBackend *native = NULL;
    TiredBackend backend = {0};
    TiredServiceInventory inventory = {0};
    TiredTransactionInventory transactions = {0};
    TiredServiceRecord record = {0};
    TiredRuntime runtime = {0};
    TiredServiceFiles files = {0};
    TiredText base = {0}, unit = {0};
    TiredAccount account = {0};
    TiredGroup group = {0};
    TiredError observation = {0};
    struct json_object *root = json_object_new_object(), *checks = json_object_new_array();
    bool ok = false;
    *status = TIRED_OK;
    if (root == NULL || checks == NULL || !tired_layout_discover(user, &layout, error))
        goto done;
    bool manager = tired_backend_open(&layout, &native, &backend, &observation);
    if (!check(
            checks, "manager", manager ? "info" : "error",
            manager
                ? "Selected native systemd manager is available and satisfies the version baseline."
            : observation.message == NULL ? "Manager is unavailable."
                                          : observation.message,
            manager ? "No action required."
                    : "Use a systemd host/current user manager; use plan --offline for inspection "
                      "without a manager."))
        goto done;
    if (!manager)
        *status = observation.status;
    if (manager)
    {
        const char *ids[] = {"credentials-feature", "memory-controller", "cpu-controller",
                             "tasks-controller", "ambient-capabilities-feature"};
        for (size_t i = 0; i < 5; ++i)
            if (!check(checks, ids[i],
                       backend.features.features[i] == TIRED_FACT_TRUE ? "info" : "warning",
                       backend.features.features[i] == TIRED_FACT_TRUE
                           ? "Feature is available for this manager."
                       : backend.features.features[i] == TIRED_FACT_FALSE
                           ? "Feature is unavailable for this manager."
                           : "Feature availability could not be established.",
                       "Select supported fields explicitly; unavailable mandatory controls are "
                       "refused."))
                goto done;
    }
    sd_id128_t machine;
    char machine_id[33];
    TiredJournalAccess access = {0};
    bool journal = sd_id128_get_machine(&machine) >= 0;
    if (journal)
    {
        sd_id128_to_string(machine, machine_id);
        journal =
            tired_journal_access_check(NULL, machine_id, user, getuid(), &access, &observation);
    }
    if (!check(checks, "journal-access", journal && access.complete ? "info" : "warning",
               !journal           ? "Journal access could not be established."
               : !access.complete ? "Some journal files are unreadable or could not be opened."
               : access.files_opened == 0 ? "No journal files are currently available."
                                          : "Accessible local journal files were checked.",
               "Use logs for unit-scoped records; request administrator access when the host "
               "restricts journals."))
        goto done;
    bool pending = tired_transaction_inventory_load(&layout, &transactions, &observation);
    if (!check(checks, "transactions",
               !pending || !transactions.complete || transactions.pending_names.count ? "warning"
                                                                                      : "info",
               !pending || !transactions.complete
                   ? "Transaction inventory could not be completely inspected."
               : transactions.pending_names.count
                   ? "Interrupted operations require explicit reconciliation."
                   : "No pending transaction was observed.",
               "Run tired recover in the selected scope; never delete recovery directories to "
               "bypass a conflict."))
        goto done;
    if (request->arguments.count == 0)
    {
        bool records = tired_service_inventory_load(&layout, &inventory, &observation);
        if (!check(checks, "records", records && inventory.complete ? "info" : "warning",
                   records && inventory.complete
                       ? "Managed-record inventory is readable and unambiguous."
                       : "Managed records contain unreadable, malformed or ambiguous entries.",
                   "Use list/status/show to inspect each entry. Administrative records require "
                   "administrator access."))
            goto done;
    }
    else
    {
        const TiredText *operand = &request->arguments.items[0];
        const TiredServiceMetadata *metadata = NULL;
        if (!tired_name_explicit(operand->data, operand->length, &base, error) ||
            !tired_name_candidate(&base, 1, &unit, error) ||
            !tired_service_inventory_load(&layout, &inventory, error) ||
            !tired_service_inventory_find(&inventory, unit.data, &metadata, error) ||
            !tired_service_record_load(&layout, metadata->service_uuid, &record, error) ||
            !tired_service_files_inspect(&layout, &record, &files, error))
            goto done;
        if (!check(checks, "unit-integrity",
                   files.unit.state == TIRED_SERVICE_FILE_MATCH ? "info" : "error",
                   files.unit.state == TIRED_SERVICE_FILE_MATCH
                       ? "Unit ownership, UUID marker, mode and content match the saved record."
                       : "Unit file is missing, unsafe, changed or unobservable.",
                   "Inspect show --unit; review foreign edits before explicitly restoring managed "
                   "configuration.") ||
            !check(checks, "owned-environment",
                   !record.has_environment || files.environment.state == TIRED_SERVICE_FILE_MATCH
                       ? "info"
                       : "error",
                   !record.has_environment ? "No owned environment file is required."
                   : files.environment.state == TIRED_SERVICE_FILE_MATCH
                       ? "Private environment revision matches its recorded digest."
                       : "Private environment revision is missing, unsafe or changed.",
                   "Restore the recorded immutable revision or make an explicit validated edit.") ||
            !path_check(checks, "executable", record.executable.lexical_path.data, false, true) ||
            !path_check(checks, "working-directory",
                        record.spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text.data, true,
                        false))
            goto done;
        const TiredText *run_as = &record.spec.fields[TIRED_FIELD_RUN_AS].value.text;
        const TiredText *group_name = &record.spec.fields[TIRED_FIELD_GROUP].value.text;
        bool accounts =
            tired_account_resolve(run_as->data, run_as->length, &account, &observation) &&
            tired_group_resolve(group_name->data, group_name->length, &group, &observation) &&
            account.uid == record.metadata.service_uid && group.gid == record.metadata.service_gid;
        if (!check(checks, "identity", accounts ? "info" : "error",
                   accounts ? "Selected account/group resolve to the recorded IDs."
                            : "Selected account/group is missing or changed.",
                   "Select the intended account explicitly; do not infer root from an application "
                   "filename."))
            goto done;
        if (manager)
        {
            /* Borrow the loaded record for the same read-only execution checks
             * used before activation. This neither stages files nor admits jobs. */
            TiredMutation inspected = {.operation = TIRED_TRANSACTION_START,
                                       .actor_uid = getuid(),
                                       .proposed = record,
                                       .root_previously_selected =
                                           record.metadata.service_uid == 0};
            TiredRiskReport risks = {0};
            TiredError readiness = {0};
            bool ready =
                tired_mutation_workload_check(&inspected, &layout, &backend, &risks, &readiness);
            bool identity_check = geteuid() == 0 || (getuid() == record.metadata.service_uid &&
                                                     getgid() == record.metadata.service_gid);
            if (!check(checks, "workload-access", ready && identity_check ? "info" : "warning",
                       !ready           ? readiness.message
                       : identity_check ? "Current workload identity, path access and selected "
                                          "features passed read-only checks."
                                        : "The current caller cannot independently inspect access "
                                          "under the selected workload identity.",
                       "Review account/path permissions and the reported feature; an administrator "
                       "can inspect another identity. No workload was started."))
                goto done;
        }
        if (manager && backend.query(backend.context, unit.data, true, &runtime, &observation))
        {
            bool fragment = runtime.fragment.data != NULL &&
                            strcmp(runtime.fragment.data, record.unit_path.data) == 0 &&
                            runtime.drop_ins.count == 0;
            if (!check(checks, "effective-fragment", fragment ? "info" : "error",
                       fragment
                           ? "The manager loaded the expected fragment without drop-ins."
                           : "Effective fragment or drop-ins change the managed configuration.",
                       "Inspect show --effective and the applicable native unit configuration "
                       "before applying changes.") ||
                !check(checks, "runtime", runtime.running ? "info" : "warning",
                       runtime.active_state.data == NULL ? "Active state is unknown."
                                                         : runtime.active_state.data,
                       "Inspect status and logs; running is not proof of application readiness.") ||
                !check(checks, "enablement", runtime.enabled ? "info" : "warning",
                       runtime.file_state.data == NULL ? "Enablement is unknown."
                                                       : runtime.file_state.data,
                       "Use enable for future activation; use --now only when immediate start is "
                       "intended."))
                goto done;
            char restarts[128], exit[256];
            if (runtime.restarts_known)
                (void)snprintf(restarts, sizeof(restarts), "Observed restart count: %llu.",
                               (unsigned long long)runtime.restarts);
            else
                (void)snprintf(restarts, sizeof(restarts), "Restart count is unavailable.");
            if (runtime.exit_known)
                (void)snprintf(exit, sizeof(exit), "Last exit code=%lld status=%lld; result=%s.",
                               (long long)runtime.exit_code, (long long)runtime.exit_status,
                               runtime.result.data == NULL ? "unknown" : runtime.result.data);
            else
                (void)snprintf(exit, sizeof(exit), "Last exit details unavailable; result=%s.",
                               runtime.result.data == NULL ? "unknown" : runtime.result.data);
            if (!check(checks, "restart-count", runtime.restarts_known ? "info" : "warning",
                       restarts,
                       "Use logs to identify repeated failures. Limited retries may require an "
                       "explicit reset/start.") ||
                !check(checks, "last-exit", runtime.failed ? "warning" : "info", exit,
                       "Inspect exit status and application logs before changing privileges or "
                       "resource limits."))
                goto done;
        }
        else if (manager &&
                 !check(checks, "runtime", "error",
                        observation.message == NULL ? "Runtime query failed." : observation.message,
                        "Reconnect to the selected manager and run status again."))
            goto done;
        const TiredFieldValue *nofile = &record.spec.fields[TIRED_FIELD_NOFILE_HARD];
        bool compatible = !tired_field_has_value(nofile) ||
                          (manager && backend.features.nofile_known &&
                           tired_limit_le(nofile->value.limit, backend.features.nofile_ceiling));
        if (!check(
                checks, "nofile", compatible ? "info" : "warning",
                compatible
                    ? "NOFILE is inherited or fits the observed host/scope ceiling."
                    : "Requested NOFILE cannot be shown compatible with the current scope ceiling.",
                "Choose compatible soft/hard values or explicitly inherit; global limits are never "
                "changed automatically."))
            goto done;
        const TiredTextList *environment =
            &record.spec.fields[TIRED_FIELD_ENVIRONMENT_FILES].value.list;
        for (size_t i = 0; i < environment->count; ++i)
            if (!path_check(checks, "external-environment", environment->items[i].data, false,
                            false))
                goto done;
        for (size_t i = 0; i < record.credentials.count; ++i)
            if (!path_check(checks, "credential", record.credentials.items[i].path.data, false,
                            false))
                goto done;
        for (size_t i = 0; i < record.external_config_paths.count; ++i)
            if (!path_check(checks, "application-config",
                            record.external_config_paths.items[i].data, false, false))
                goto done;
        if (!check(checks, "network-policy", "info",
                   tired_spec_choice_is(&record.spec, TIRED_FIELD_NETWORK, "online")
                       ? "network-online ordering is selected; an applicable wait service is not "
                         "established by this ordering."
                       : "No continuous network-health monitoring is configured.",
                   "Inspect wait-online and application prerequisites yourself; tired never "
                   "enables a wait implementation automatically."))
            goto done;
    }
    if (user)
    {
        TiredLingerObservation linger = {0};
        tired_linger_observe(2000, &linger);
        if (!check(checks, "lingering",
                   linger.result.done && linger.result.error.status == TIRED_OK &&
                           linger.result.enabled
                       ? "info"
                       : "warning",
                   linger.result.error.status != TIRED_OK ? "Current account lingering is unknown."
                   : linger.result.enabled
                       ? "Current account lingering is enabled."
                       : "Current account lingering is disabled; activation may depend on login.",
                   "Explicit --enable-linger is an account-level change. Removing a service never "
                   "disables lingering."))
            goto done;
    }
    if (request->json)
    {
        bool inserted = put(root, "checks", checks);
        checks = NULL;
        if (!inserted || !put(root, "schema_version", json_object_new_int(1)) ||
            !put(root, "command", json_object_new_string("doctor")) ||
            !put(root, "ok", json_object_new_boolean(*status == TIRED_OK)) ||
            !put(root, "exit_code", json_object_new_int(*status)))
            goto done;
        const char *encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        ok = encoded != NULL &&
             tired_text_set(output, encoded, strlen(encoded), TIRED_INPUT_LIMIT, error);
    }
    else
    {
        TiredBuffer buffer;
        tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
        ok = true;
        for (size_t i = 0; ok && i < json_object_array_length(checks); ++i)
        {
            struct json_object *item = json_object_array_get_idx(checks, i), *id, *severity,
                               *message, *correction;
            bool fields = json_object_object_get_ex(item, "id", &id) &&
                          json_object_object_get_ex(item, "severity", &severity) &&
                          json_object_object_get_ex(item, "observation", &message) &&
                          json_object_object_get_ex(item, "correction", &correction);
            if (!fields)
            {
                ok = false;
                break;
            }
            char line[2048];
            int n = snprintf(line, sizeof(line), "[%s] %s: %s\n  %s\n",
                             json_object_get_string(severity), json_object_get_string(id),
                             json_object_get_string(message), json_object_get_string(correction));
            ok = n > 0 && (size_t)n < sizeof(line) &&
                 tired_buffer_append(&buffer, line, (size_t)n, error);
        }
        if (ok)
            ok = tired_buffer_take(&buffer, output, error);
        tired_buffer_destroy(&buffer);
    }
done:
    json_object_put(root);
    json_object_put(checks);
    tired_backend_destroy(native);
    tired_service_record_destroy(&record);
    tired_service_inventory_destroy(&inventory);
    tired_transaction_inventory_destroy(&transactions);
    tired_runtime_destroy(&runtime);
    tired_account_destroy(&account);
    tired_group_destroy(&group);
    tired_text_destroy(&base);
    tired_text_destroy(&unit);
    tired_layout_destroy(&layout);
    if (!ok && error->status == TIRED_OK)
        tired_error_set(error, TIRED_INTERNAL, "doctor-output", "Cannot produce diagnostic checks.",
                        0);
    return ok;
}
