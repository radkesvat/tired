#define _GNU_SOURCE
#include "tired/capture.h"
#include "tired/collision.h"
#include "tired/encode.h"
#include "tired/file_fingerprint.h"
#include "tired/helper.h"
#include "tired/io.h"
#include "tired/mutation.h"
#include "tired/name.h"
#include "tired/process_value.h"
#include "tired/redaction.h"
#include "tired/render.h"
#include "tired/service_files.h"
#include "tired/service_record_storage.h"
#include "tired/transaction_inventory.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

bool tired_directory_ensure(const char *path, bool private_final, TiredDirectory **output,
                            TiredError *error)
{
    if (path == NULL || path[0] != '/' || strlen(path) > 4096)
        return tired_error_set(error, TIRED_INVALID, "directory-path", "Invalid state path.", 0);
    TiredDirectory *current = NULL;
    if (!tired_directory_open("/", geteuid(), false, &current, error))
        return false;
    const char *position = path + 1;
    bool ok = false;
    while (*position != '\0')
    {
        const char *slash = strchr(position, '/');
        size_t length = slash == NULL ? strlen(position) : (size_t)(slash - position);
        if (length == 0 || length > 255 || (length == 1 && position[0] == '.') ||
            (length == 2 && memcmp(position, "..", 2) == 0))
        {
            tired_error_set(error, TIRED_INVALID, "directory-component",
                            "State paths require normalized safe components.", 0);
            goto done;
        }
        char name[256];
        memcpy(name, position, length);
        name[length] = '\0';
        TiredDirectory *next = NULL;
        bool final = slash == NULL;
        size_t prefix = (size_t)(position - path) + length;
        bool public_config =
            (prefix == strlen("/etc/tired") && strncmp(path, "/etc/tired", prefix) == 0) ||
            (prefix == strlen("/etc/tired/profiles.d") &&
             strncmp(path, "/etc/tired/profiles.d", prefix) == 0);
        if (!tired_directory_child_mode(current, name, true,
                                        final && private_final && !public_config,
                                        public_config ? 0755 : 0700, &next, error))
            goto done;
        tired_directory_destroy(current);
        current = next;
        if (final)
            break;
        position = slash + 1;
    }
    *output = current;
    current = NULL;
    ok = true;
done:
    tired_directory_destroy(current);
    return ok;
}
static bool has_drop_ins(const TiredTextList *paths, const char *leaf, TiredError *error)
{
    for (size_t i = 0; i < paths->count; ++i)
    {
        TiredText path = {0};
        if (!tired_path_absolute(&paths->items[i], leaf, strlen(leaf), &path, error))
            return false;
        DIR *directory = opendir(path.data);
        int saved = errno;
        tired_text_destroy(&path);
        if (directory == NULL)
        {
            if (saved == ENOENT || saved == ENOTDIR)
                continue;
            return tired_error_set(error, TIRED_CONFLICT, "drop-in-access",
                                   "Cannot inspect applicable service drop-ins.", saved);
        }
        bool found = false;
        struct dirent *entry;
        size_t count = 0;
        errno = 0;
        while ((entry = readdir(directory)) != NULL)
        {
            size_t length = strlen(entry->d_name);
            if (++count > 4096 || (length > 5 && strcmp(entry->d_name + length - 5, ".conf") == 0))
            {
                found = true;
                break;
            }
        }
        saved = errno;
        if (closedir(directory) != 0 && saved == 0)
            saved = errno;
        if (found || saved != 0)
            return tired_error_set(error, TIRED_CONFLICT, "effective-drop-ins",
                                   "Applicable drop-ins require explicit native review; they may "
                                   "change the approved service. No service changes were made.",
                                   saved);
    }
    return true;
}
static bool inspect_privileged(const char *path, bool *writable, TiredError *error)
{
    char *copy = strdup(path);
    if (copy == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot inspect code path.", 0);
    bool ok = true;
    for (char *position = copy + 1;; ++position)
        if (*position == '/' || *position == '\0')
        {
            char saved = *position;
            *position = '\0';
            struct stat file;
            if (stat(copy, &file) != 0)
            {
                ok = tired_error_set(error, TIRED_INVALID, "privileged-code-access",
                                     "Cannot inspect privileged executable ancestry.", errno);
                break;
            }
            *writable |=
                (file.st_mode & 0022) != 0 || (file.st_uid != 0 && (file.st_mode & 0200) != 0);
            *position = saved;
            if (saved == '\0')
                break;
        }
    free(copy);
    return ok;
}
static bool account_access(const TiredServiceRecord *record, const TiredAccount *account,
                           const TiredGroup *group, const TiredLayout *layout, TiredError *error)
{
    const char *executable = record->spec.fields[TIRED_FIELD_EXECUTABLE].value.text.data;
    const char *directory = record->spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text.data;
    /* An unprivileged review cannot switch identity. The helper repeats this test
     * under its own authority before committing a system-scope request. */
    const TiredTextList *extra = &record->spec.fields[TIRED_FIELD_SUPPLEMENTARY_GROUPS].value.list;
    if (geteuid() != 0 && (geteuid() != account->uid || getegid() != group->gid ||
                           (!layout->user_scope && extra->count != 0)))
        return true;
    pid_t child = fork();
    if (child < 0)
        return tired_error_set(error, TIRED_INTERNAL, "access-fork",
                               "Cannot check workload access.", errno);
    if (child == 0)
    {
        if (geteuid() == 0)
        {
            if (initgroups(account->name.data, group->gid) != 0)
                _exit(1);
            int inherited = getgroups(0, NULL);
            if (inherited < 0 || inherited > 65536 || extra->count > 65536 - (size_t)inherited)
                _exit(1);
            size_t count = (size_t)inherited;
            gid_t *groups = calloc(count + extra->count + 1, sizeof(*groups));
            if (groups == NULL || getgroups(inherited, groups) != inherited)
                _exit(1);
            for (size_t i = 0; i < extra->count; ++i)
            {
                TiredGroup selected = {0};
                if (!tired_group_resolve(extra->items[i].data, extra->items[i].length, &selected,
                                         error))
                    _exit(1);
                bool duplicate = false;
                for (size_t j = 0; j < count; ++j)
                    duplicate |= groups[j] == selected.gid;
                if (!duplicate)
                    groups[count++] = selected.gid;
                tired_group_destroy(&selected);
            }
            if (setgroups(count, groups) != 0)
                _exit(1);
            free(groups);
            if (setgid(group->gid) != 0 || setuid(account->uid) != 0)
                _exit(1);
        }
        if (access(executable, X_OK) != 0 || access(directory, X_OK) != 0)
            _exit(1);
        for (size_t i = 0; i < record->external_config_paths.count; ++i)
            if (access(record->external_config_paths.items[i].data, R_OK) != 0)
                _exit(1);
        _exit(0);
    }
    int status;
    pid_t waited;
    do
        waited = waitpid(child, &status, 0);
    while (waited < 0 && errno == EINTR);
    return (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) ||
           tired_error_set(error, TIRED_INVALID, "workload-access",
                           "The selected workload account cannot access its executable, working "
                           "directory or selected configuration. Choose accessible paths/account.",
                           0);
}
static bool recorded_state(const TiredMutation *mutation, const TiredLayout *layout,
                           TiredRuntime *runtime, const TiredBackend *backend, bool *previous_root,
                           TiredError *error)
{
    const TiredServiceRecord *proposed = &mutation->proposed;
    TiredServiceRecord old = {0};
    TiredServiceFiles files = {0};
    TiredDirectory *records = NULL;
    TiredFileFingerprint fingerprint = {0};
    char filename[42];
    bool ok = false;
    TiredMutation fetched = {0};
    bool privileged_read = !layout->user_scope && geteuid() != 0;
    if (privileged_read)
    {
        const char *lookup = mutation->previous_name.length != 0
                                 ? mutation->previous_name.data
                                 : proposed->metadata.unit_name.data;
        if (!tired_helper_record_call(lookup, false, false, &fetched, error))
            goto done;
        old = fetched.proposed;
        fetched.proposed = (TiredServiceRecord){0};
        if (strcmp(fetched.expected_unit_sha256.data, mutation->expected_unit_sha256.data) != 0 ||
            strcmp(fetched.expected_record_sha256.data, mutation->expected_record_sha256.data) != 0)
        {
            tired_error_set(error, TIRED_CONFLICT, "managed-record-changed",
                            "Managed state changed since review.", 0);
            goto done;
        }
    }
    else if (!tired_service_record_load(layout, proposed->metadata.service_uuid, &old, error))
        goto done;
    const char *name = mutation->previous_name.data;
    if (name == NULL || name[0] == '\0')
        name = proposed->metadata.unit_name.data;
    if (strcmp(name, old.metadata.unit_name.data) != 0 ||
        mutation->expected_unit_sha256.length != 64 ||
        mutation->expected_record_sha256.length != 64 ||
        proposed->metadata.created_usec != old.metadata.created_usec ||
        proposed->metadata.invoking_uid != old.metadata.invoking_uid ||
        proposed->metadata.invoking_gid != old.metadata.invoking_gid ||
        (old.linger_requested && !proposed->linger_requested) ||
        (proposed->linger_requested && !old.linger_requested &&
         !proposed->spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean))
    {
        tired_error_set(error, TIRED_CONFLICT, "record-precondition",
                        "The reviewed service identity changed.", 0);
        goto done;
    }
    if (!tired_service_files_inspect(layout, &old, &files, error))
        goto done;
    bool unit_ok =
        files.unit.state == TIRED_SERVICE_FILE_MATCH ||
        (mutation->restore_drift && files.unit.state == TIRED_SERVICE_FILE_DRIFTED &&
         files.unit.owner_matches && files.unit.mode_matches && files.unit.marker_matches);
    if (!unit_ok ||
        (!privileged_read && old.has_environment &&
         files.environment.state != TIRED_SERVICE_FILE_MATCH) ||
        strcmp(files.unit.actual.sha256, mutation->expected_unit_sha256.data) != 0)
    {
        tired_error_set(
            error, TIRED_CONFLICT, "managed-file-drift",
            "Managed files are missing, unsafe or changed since review. Inspect show/doctor.", 0);
        goto done;
    }
    if (!privileged_read && !tired_directory_open(layout->paths[TIRED_PATH_RECORDS].data,
                                                  old.metadata.owner_uid, true, &records, error))
        goto done;
    (void)snprintf(filename, sizeof(filename), "%s.json", old.metadata.service_uuid);
    if (!privileged_read &&
        !tired_file_fingerprint(records, filename, TIRED_SERVICE_RECORD_LIMIT, &fingerprint, error))
        goto done;
    if (!privileged_read &&
        (!fingerprint.exists ||
         strcmp(fingerprint.sha256, mutation->expected_record_sha256.data) != 0))
    {
        tired_error_set(error, TIRED_CONFLICT, "managed-record-changed",
                        "Service record changed since review.", 0);
        goto done;
    }
    if (!backend->query(backend->context, name, true, runtime, error))
        goto done;
    if (mutation->operation > TIRED_TRANSACTION_RESTORE ||
        mutation->operation == TIRED_TRANSACTION_REMOVE)
    {
        TiredText unit = {0};
        char sha[65];
        bool same =
            proposed->metadata.service_uid == old.metadata.service_uid &&
            proposed->metadata.service_gid == old.metadata.service_gid &&
            strcmp(proposed->metadata.revision_uuid, old.metadata.revision_uuid) == 0 &&
            tired_render_unit(&proposed->spec, proposed->metadata.service_uuid,
                              proposed->has_environment ? &proposed->environment_path : NULL,
                              &proposed->credentials, &unit, error) &&
            tired_digest_bytes(unit.data, unit.length, sha, error) &&
            strcmp(sha, old.metadata.unit_sha256) == 0 &&
            strcmp(proposed->metadata.unit_sha256, old.metadata.unit_sha256) == 0;
        tired_text_destroy(&unit);
        if (!same)
        {
            tired_error_set(error, TIRED_CONFLICT, "lifecycle-model-changed",
                            "A lifecycle operation cannot replace the saved service configuration. "
                            "Use an explicit edit transaction.",
                            0);
            goto done;
        }
    }
    if (!runtime->found || !runtime->loaded || runtime->fragment.data == NULL ||
        strcmp(runtime->fragment.data, old.unit_path.data) != 0 || runtime->drop_ins.count != 0 ||
        !runtime->job_known || runtime->job_id != 0)
    {
        tired_error_set(
            error, TIRED_CONFLICT, "manager-fragment-conflict",
            "The effective manager fragment/drop-ins differ from the managed configuration.", 0);
        goto done;
    }
    *previous_root = old.metadata.service_uid == 0;
    ok = true;
done:
    tired_directory_destroy(records);
    tired_service_record_destroy(&old);
    tired_mutation_destroy(&fetched);
    return ok;
}
static bool execution_inputs(const TiredServiceRecord *record, const TiredLayout *layout,
                             const TiredBackend *backend, TiredError *error)
{
    TiredServiceRecord hydrated = {0};
    TiredText bytes = {0};
    const TiredServiceRecord *values = record;
    bool ok = false;
    /* A private revision may be read only by the committing authority. Never
     * hydrate a shallow copy: environment containers own their allocations. */
    if (record->has_environment && !record->environment_loaded &&
        (layout->user_scope || geteuid() == 0))
    {
        if (!tired_service_record_encode(record, &bytes, error) ||
            !tired_service_record_parse(bytes.data, bytes.length, &hydrated, error) ||
            !tired_service_record_hydrate(&hydrated, layout, error))
            goto done;
        values = &hydrated;
    }
    long page = sysconf(_SC_PAGESIZE), fallback = sysconf(_SC_ARG_MAX);
    uint64_t single = page > 0 ? (uint64_t)page * 32 : 131072;
    uint64_t maximum = backend->argument_max != 0 ? backend->argument_max
                       : fallback > 0             ? (uint64_t)fallback
                                                  : 131072;
    uint64_t total = 8192; /* manager-generated environment and credential paths */
    const TiredTextList *argv = &record->spec.fields[TIRED_FIELD_ARGV].value.list;
    for (size_t i = 0; i < argv->count; ++i)
    {
        const TiredText *word = i == 0 ? &record->executable.lexical_path : &argv->items[i];
        if (word->length + 1 > single)
            goto limit;
        total += word->length + 1 + sizeof(char *);
    }
    for (size_t i = 0; i < values->environment.count; ++i)
    {
        const TiredEnvironmentEntry *entry = &values->environment.items[i];
        uint64_t length = entry->name.length + entry->value.length + 2;
        if (length > single)
            goto limit;
        total += length + sizeof(char *);
    }
    if (total > maximum)
        goto limit;
    /* These sources are loaded by the selected manager, before the workload
     * changes identity. The root helper repeats unprivileged system preflight. */
    if (layout->user_scope || geteuid() == 0)
    {
        const TiredTextList *files = &record->spec.fields[TIRED_FIELD_ENVIRONMENT_FILES].value.list;
        for (size_t i = 0; i < files->count + record->credentials.count; ++i)
        {
            const char *path = i < files->count
                                   ? files->items[i].data
                                   : record->credentials.items[i - files->count].path.data;
            struct stat file;
            if (stat(path, &file) != 0 || !S_ISREG(file.st_mode) || access(path, R_OK) != 0)
            {
                tired_error_set(error, TIRED_INVALID, "manager-input-access",
                                "The selected manager cannot read an external environment or "
                                "credential file. Choose an accessible regular file.",
                                errno);
                goto done;
            }
        }
    }
    ok = true;
    goto done;
limit:
    tired_error_set(error, TIRED_INVALID, "execution-input-limit",
                    "Arguments and captured environment exceed the selected manager's execution "
                    "limit. Shorten individual values or move large data to application files.",
                    0);
done:
    tired_text_destroy(&bytes);
    tired_service_record_destroy(&hydrated);
    return ok;
}
bool tired_mutation_validate(const TiredMutation *mutation, const TiredLayout *layout,
                             const TiredBackend *backend, TiredRiskReport *risks, TiredError *error)
{
    const TiredServiceRecord *record = &mutation->proposed;
    TiredRuntime runtime = {0};
    TiredTransactionInventory pending = {0};
    bool previous_root = false;
    bool ok = false;
    char digest[65];
    if ((unsigned)mutation->operation >= TIRED_TRANSACTION_OPERATION_COUNT ||
        mutation->observation_usec > 30000000 || mutation->history_revisions > 1000 ||
        !tired_service_record_check_layout(record, layout, error) ||
        !tired_spec_validate_scalars(&record->spec, error) ||
        !tired_mutation_digest(mutation, digest, error))
        goto done;
    if (mutation->operation == TIRED_TRANSACTION_CREATE &&
        record->linger_requested != record->spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean)
    {
        tired_error_set(error, TIRED_INVALID, "linger-intent", "Invalid account lingering intent.",
                        0);
        goto done;
    }
    if (!mutation->recovery)
    {
        TiredText transaction = {0};
        struct stat existing;
        bool derived =
            tired_path_absolute(&layout->paths[TIRED_PATH_TRANSACTIONS],
                                record->metadata.transaction_uuid, 36, &transaction, error);
        bool reused = derived && lstat(transaction.data, &existing) == 0;
        int saved = errno;
        tired_text_destroy(&transaction);
        if (!derived)
            goto done;
        if (reused)
        {
            tired_error_set(error, TIRED_CONFLICT, "transaction-reused",
                            "This transaction identity has already been submitted.", 0);
            goto done;
        }
        if (saved != ENOENT && !(saved == EACCES && geteuid() != 0 && !layout->user_scope))
        {
            tired_error_set(error, TIRED_AUTHORIZATION, "transaction-access",
                            "Cannot inspect the transaction destination.", saved);
            goto done;
        }
    }
    if (strcmp(digest, record->review.approved_sha256) != 0)
    {
        tired_error_set(error, TIRED_CONFLICT, "approval-changed",
                        "Approved model or operation changed.", 0);
        goto done;
    }
    if (layout->user_scope &&
        (record->metadata.invoking_uid != getuid() || record->metadata.service_uid != getuid() ||
         record->metadata.service_gid != getgid()))
    {
        tired_error_set(error, TIRED_AUTHORIZATION, "user-identity",
                        "User scope cannot change execution identity.", 0);
        goto done;
    }
    if (!tired_layout_check_unit_path(layout, backend->load_paths, error))
        goto done;
    if (!tired_transaction_inventory_load(layout, &pending, error))
    {
        if (!layout->user_scope && geteuid() != 0 && error->status == TIRED_AUTHORIZATION)
        {
            /* Unprivileged proposal only. The administrative helper repeats the
             * complete inventory check as root before acquiring commit authority. */
            pending.complete = true;
            tired_error_clear(error);
        }
        else
            goto done;
    }
    const TiredTextList *reserved = NULL;
    if (!tired_transaction_inventory_pending(&pending, &reserved, error))
        goto done;
    /* Scope-wide lock serializes every writer. Incomplete journals block mutation
     * until explicit reconciliation; read-only inspection remains available. */
    if (reserved->count != 0)
    {
        tired_error_set(error, TIRED_RECOVERY_REQUIRED, "pending-transaction",
                        "An interrupted operation requires tired recover before another mutation.",
                        0);
        goto done;
    }
    if (!has_drop_ins(backend->load_paths, "service.d", error))
        goto done;
    const char *name = record->metadata.unit_name.data;
    for (size_t i = 0; name[i] != '\0'; ++i)
        if (name[i] == '-')
        {
            char prefix[256];
            if (i + sizeof(".service.d") + 1 > sizeof(prefix))
                goto done;
            memcpy(prefix, name, i + 1);
            memcpy(prefix + i + 1, ".service.d", sizeof(".service.d"));
            if (!has_drop_ins(backend->load_paths, prefix, error))
                goto done;
        }
    if (mutation->operation == TIRED_TRANSACTION_CREATE ||
        mutation->operation == TIRED_TRANSACTION_RENAME)
    {
        if (!backend->query(backend->context, name, false, &runtime, error))
            goto done;
        TiredUnitQueryResult state = {.done = true,
                                      .file_found = runtime.found,
                                      .object_found = runtime.loaded,
                                      .unit_name = name};
        TiredCollision collision = {0};
        bool checked = tired_collision_check(&record->metadata.unit_name, &state,
                                             backend->load_paths, reserved, &collision, error);
        bool clear = checked && collision.kind == TIRED_COLLISION_NONE;
        tired_collision_destroy(&collision);
        if (!clear)
        {
            if (checked)
                tired_error_set(error, TIRED_CONFLICT, "name-collision",
                                "The proposed unit name is reserved.", 0);
            goto done;
        }
    }
    if (mutation->operation != TIRED_TRANSACTION_CREATE &&
        !recorded_state(mutation, layout, &runtime, backend, &previous_root, error))
        goto done;
    TiredMutation context = *mutation;
    context.root_previously_selected = previous_root;
    ok = tired_mutation_workload_check(&context, layout, backend, risks, error);

done:
    tired_runtime_destroy(&runtime);
    tired_transaction_inventory_destroy(&pending);
    return ok;
}

bool tired_mutation_workload_check(const TiredMutation *mutation, const TiredLayout *layout,
                                   const TiredBackend *backend, TiredRiskReport *risks,
                                   TiredError *error)
{
    const TiredServiceRecord *record = &mutation->proposed;
    TiredAccount account = {0};
    TiredGroup group = {0};
    TiredText unit = {0};
    bool ok = false, writable = false;
    char digest[65];
    if (!tired_service_record_check_layout(record, layout, error) ||
        !tired_spec_validate_scalars(&record->spec, error))
        goto done;
    /* Lifecycle/removal can repair unavailable applications without executing
     * their command. Installation/edit/rename revalidate the workload context. */
    if (mutation->operation == TIRED_TRANSACTION_CREATE ||
        mutation->operation == TIRED_TRANSACTION_EDIT ||
        mutation->operation == TIRED_TRANSACTION_RENAME ||
        mutation->operation == TIRED_TRANSACTION_RESTORE ||
        mutation->operation == TIRED_TRANSACTION_START ||
        mutation->operation == TIRED_TRANSACTION_RESTART ||
        (mutation->operation == TIRED_TRANSACTION_ENABLE && mutation->now))
    {
        const TiredTextList *capabilities =
            &record->spec.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].value.list;
        for (size_t i = 0; i < capabilities->count; ++i)
        {
            unsigned capability;
            if (!tired_parse_capability(capabilities->items[i].data, capabilities->items[i].length,
                                        &capability, error) ||
                !backend->capabilities_known ||
                (backend->permitted_capabilities & (UINT64_C(1) << capability)) == 0)
            {
                tired_error_set(error, TIRED_UNSUPPORTED, "capability-unavailable",
                                "The selected manager cannot grant a requested ambient capability. "
                                "Choose an available capability or inherit the field.",
                                0);
                goto done;
            }
        }
        if (record->credentials.count != 0 && backend->features.features[0] != TIRED_FACT_TRUE)
        {
            tired_error_set(error, TIRED_UNSUPPORTED, "credentials-unavailable",
                            "Credential loading is unavailable for this manager.", 0);
            goto done;
        }
        const TiredFieldId fields[] = {TIRED_FIELD_MEMORY_MAX, TIRED_FIELD_CPU_QUOTA,
                                       TIRED_FIELD_TASKS_MAX, TIRED_FIELD_AMBIENT_CAPABILITIES};
        for (size_t i = 0; i < 4; ++i)
        {
            const TiredFieldValue *field = &record->spec.fields[fields[i]];
            bool selected =
                tired_field_has_value(field) && (i != 3 || field->value.list.count != 0);
            if (selected && backend->features.features[i + 1] != TIRED_FACT_TRUE)
            {
                tired_error_set(error, TIRED_UNSUPPORTED, "selected-feature-unavailable",
                                "A selected resource controller or capability feature is "
                                "unavailable or unknown for this manager. Inspect doctor and "
                                "explicitly inherit the unsupported field.",
                                0);
                goto done;
            }
        }
        if (layout->user_scope && record->metadata.service_uid != 0 &&
            ((tired_field_has_value(&record->spec.fields[TIRED_FIELD_PRIVATE_TMP]) &&
              record->spec.fields[TIRED_FIELD_PRIVATE_TMP].value.boolean) ||
             (tired_field_has_value(&record->spec.fields[TIRED_FIELD_PROTECT_SYSTEM]) &&
              !tired_spec_choice_is(&record->spec, TIRED_FIELD_PROTECT_SYSTEM, "false")) ||
             (tired_field_has_value(&record->spec.fields[TIRED_FIELD_PROTECT_HOME]) &&
              !tired_spec_choice_is(&record->spec, TIRED_FIELD_PROTECT_HOME, "false")) ||
             record->spec.fields[TIRED_FIELD_READ_WRITE_PATHS].value.list.count != 0))
        {
            tired_error_set(error, TIRED_UNSUPPORTED, "user-filesystem-sandbox",
                            "Filesystem namespace hardening is unavailable for nonroot user "
                            "services on the supported baseline. Choose system scope or "
                            "explicitly inherit these fields.",
                            0);
            goto done;
        }
        bool filesystem =
            (tired_field_has_value(&record->spec.fields[TIRED_FIELD_PRIVATE_TMP]) &&
             record->spec.fields[TIRED_FIELD_PRIVATE_TMP].value.boolean) ||
            (tired_field_has_value(&record->spec.fields[TIRED_FIELD_PROTECT_SYSTEM]) &&
             !tired_spec_choice_is(&record->spec, TIRED_FIELD_PROTECT_SYSTEM, "false")) ||
            (tired_field_has_value(&record->spec.fields[TIRED_FIELD_PROTECT_HOME]) &&
             !tired_spec_choice_is(&record->spec, TIRED_FIELD_PROTECT_HOME, "false")) ||
            record->spec.fields[TIRED_FIELD_READ_WRITE_PATHS].value.list.count != 0;
        if (filesystem && (!backend->capabilities_known ||
                           (backend->permitted_capabilities & (UINT64_C(1) << 21)) == 0 ||
                           access("/proc/self/ns/mnt", F_OK) != 0))
        {
            tired_error_set(error, TIRED_UNSUPPORTED, "filesystem-sandbox-unavailable",
                            "The selected manager cannot provide the requested mount namespace "
                            "hardening. Choose compatible fields or explicit inheritance.",
                            0);
            goto done;
        }
        const TiredFieldValue *nice = &record->spec.fields[TIRED_FIELD_NICE];
        if (tired_field_has_value(nice) &&
            (!backend->nice_known || nice->value.integer < backend->minimum_nice))
        {
            tired_error_set(error, TIRED_UNSUPPORTED, "nice-unavailable",
                            "The requested priority exceeds the selected manager's actual "
                            "capability or inherited priority allowance. Choose a compatible "
                            "value or explicitly inherit it.",
                            0);
            goto done;
        }
        if (mutation->operation <= TIRED_TRANSACTION_RESTORE && record->has_environment &&
            !record->environment_loaded && !mutation->recovery)
        {
            tired_error_set(
                error, TIRED_INVALID, "environment-values-required",
                "A new environment revision requires validated explicit assignment values.", 0);
            goto done;
        }
        const TiredText *run_as = &record->spec.fields[TIRED_FIELD_RUN_AS].value.text;
        const TiredText *group_name = &record->spec.fields[TIRED_FIELD_GROUP].value.text;
        if (!tired_account_resolve(run_as->data, run_as->length, &account, error) ||
            !tired_group_resolve(group_name->data, group_name->length, &group, error))
            goto done;
        if (account.uid != record->metadata.service_uid ||
            group.gid != record->metadata.service_gid)
        {
            tired_error_set(error, TIRED_CONFLICT, "account-changed",
                            "The selected account/group changed since review.", 0);
            goto done;
        }
        struct stat executable, directory;
        const char *path = record->executable.lexical_path.data;
        if (stat(path, &executable) != 0 || !S_ISREG(executable.st_mode) ||
            (executable.st_mode & 0111) == 0 ||
            stat(record->spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text.data, &directory) !=
                0 ||
            !S_ISDIR(directory.st_mode))
        {
            tired_error_set(error, TIRED_INVALID, "workload-files",
                            "Executable or working directory is unavailable.", errno);
            goto done;
        }
        if (executable.st_dev != record->executable.device ||
            executable.st_ino != record->executable.inode)
        {
            tired_error_set(error, TIRED_CONFLICT, "executable-changed",
                            "The executable target changed since review.", 0);
            goto done;
        }
        const TiredTextList *supplementary =
            &record->spec.fields[TIRED_FIELD_SUPPLEMENTARY_GROUPS].value.list;
        if (supplementary->count != 0 &&
            (!backend->capabilities_known ||
             (backend->permitted_capabilities & (UINT64_C(1) << 6)) == 0))
        {
            tired_error_set(error, TIRED_UNSUPPORTED, "supplementary-groups-unavailable",
                            "The selected manager cannot grant supplementary groups. Use "
                            "inherited groups or explicitly choose system scope.",
                            0);
            goto done;
        }
        for (size_t i = 0; i < supplementary->count; ++i)
        {
            TiredGroup selected = {0};
            bool found = tired_group_resolve(supplementary->items[i].data,
                                             supplementary->items[i].length, &selected, error);
            tired_group_destroy(&selected);
            if (!found)
                goto done;
        }
        if (!account_access(record, &account, &group, layout, error) ||
            !execution_inputs(record, layout, backend, error))
            goto done;
        if (record->metadata.service_uid == 0 ||
            record->spec.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].value.list.count != 0)
        {
            if (!inspect_privileged(path, &writable, error) ||
                !inspect_privileged(record->executable.resolved_path.data, &writable, error) ||
                !inspect_privileged(
                    record->spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text.data, &writable,
                    error))
                goto done;
        }
        const TiredFieldValue *nofile = &record->spec.fields[TIRED_FIELD_NOFILE_HARD];
        if (tired_field_has_value(nofile))
        {
            if (layout->user_scope && !backend->features.nofile_known)
            {
                tired_error_set(error, TIRED_UNSUPPORTED, "user-nofile-unavailable",
                                "Cannot establish the user manager's hard NOFILE ceiling. Inspect "
                                "doctor or explicitly inherit both NOFILE limits.",
                                0);
                goto done;
            }
            TiredText ceiling = {0};
            uint64_t max = 0;
            bool read = tired_read_file("/proc/sys/fs/nr_open", 64, &ceiling, error);
            while (ceiling.length != 0 && ceiling.data[ceiling.length - 1] == '\n')
                --ceiling.length;
            read =
                read && tired_parse_u64(ceiling.data, ceiling.length, 1, UINT64_MAX, &max, error);
            tired_text_destroy(&ceiling);
            if (!read)
                goto done;
            TiredLimit requested = nofile->value.limit;
            if (requested.infinity || requested.value > max ||
                (backend->features.nofile_known &&
                 !tired_limit_le(requested, backend->features.nofile_ceiling)))
            {
                tired_error_set(error, TIRED_INVALID, "nofile-ceiling",
                                "Requested NOFILE exceeds the observed host/scope ceiling. "
                                "Choose a compatible limit or explicitly inherit it.",
                                0);
                goto done;
            }
        }
        if (!tired_render_unit(&record->spec, record->metadata.service_uuid,
                               record->has_environment ? &record->environment_path : NULL,
                               &record->credentials, &unit, error) ||
            !tired_digest_bytes(unit.data, unit.length, digest, error))
            goto done;
        if (strcmp(digest, record->metadata.unit_sha256) != 0 ||
            !backend->verify(backend->context, &record->spec, &unit, error))
            goto done;
    }
    bool mask[TIRED_ARGUMENT_LIMIT] = {0};
    bool classified = tired_argv_classify(&record->spec.fields[TIRED_FIELD_ARGV].value.list,
                                          record->review.sensitive_arguments, mask);
    TiredRiskFacts facts = {.invoking_uid = mutation->root_previously_selected &&
                                                    record->metadata.service_uid == 0
                                                ? 0
                                                : mutation->actor_uid,
                            .service_uid = record->metadata.service_uid,
                            .privileged_code_checked = true,
                            .privileged_code_writable = writable,
                            .sensitive_command = classified,
                            .restoring_drift = mutation->restore_drift};
    tired_risk_assess(&record->spec, &facts, risks);
    bool execution = mutation->operation == TIRED_TRANSACTION_CREATE ||
                     mutation->operation == TIRED_TRANSACTION_EDIT ||
                     mutation->operation == TIRED_TRANSACTION_RENAME ||
                     mutation->operation == TIRED_TRANSACTION_RESTORE ||
                     mutation->operation == TIRED_TRANSACTION_START ||
                     mutation->operation == TIRED_TRANSACTION_RESTART ||
                     (mutation->operation == TIRED_TRANSACTION_ENABLE && mutation->now);
    if (!execution)
    {
        risks->present[TIRED_RISK_ROOT] = false;
        risks->present[TIRED_RISK_CAPABILITIES] = false;
        risks->present[TIRED_RISK_WRITABLE_CODE] = risks->pending[TIRED_RISK_WRITABLE_CODE] = false;
        risks->present[TIRED_RISK_RAPID_RETRY] = false;
        risks->present[TIRED_RISK_SENSITIVE_COMMAND] = false;
    }
    ok = true;
done:
    tired_account_destroy(&account);
    tired_group_destroy(&group);
    tired_text_destroy(&unit);
    return ok;
}
