#include "recover_files.h"
#include "recover_live.h"
#include "tired/encode.h"
#include "tired/recover_frontend.h"
#include "tired/transaction_inventory.h"
#include <assert.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static bool text(TiredBuffer *buffer, const char *value, TiredError *error)
{
    return tired_buffer_append(buffer, value, strlen(value), error);
}
static const char *const file_states[] = {"unknown", "before", "after", "foreign"};
static const char *const artifact_states[] = {"unknown", "match", "missing", "different"};
static struct json_object *artifact_json(const TiredArtifactObservation *observed,
                                         const TiredError *error, bool required)
{
    struct json_object *result = json_object_new_object();
    if (result == NULL)
        return NULL;
    if (!add(
            result, "state",
            json_object_new_string(required ? artifact_states[observed->state] : "not_required")) ||
        (error->status != TIRED_OK && !add(result, "error", json_object_new_string(error->code))))
    {
        json_object_put(result);
        return NULL;
    }
    return result;
}
static struct json_object *file_json(const TiredRecoveryFiles *files)
{
    struct json_object *result = json_object_new_object(), *rows = json_object_new_array();
    if (result == NULL || rows == NULL)
        goto fail;
    if (!add(result, "status",
             json_object_new_string(files->error.status != TIRED_OK ? "unknown"
                                    : files->observations.complete && files->artifacts_complete
                                        ? "completed"
                                        : "partial")))
        goto fail;
    if (files->error.status != TIRED_OK)
    {
        if (!add(result, "error", json_object_new_string(files->error.code)))
            goto fail;
    }
    else
        for (size_t i = 0; i < files->observations.count; ++i)
        {
            const TiredFileObservation *observed = &files->observations.files[i];
            struct json_object *row = json_object_new_object();
            if (row == NULL)
                goto fail;
            bool ok = add(row, "manifest_index", json_object_new_uint64(i)) &&
                      add(row, "state", json_object_new_string(file_states[observed->state]));
            if (ok && observed->error.status != TIRED_OK)
                ok = add(row, "error", json_object_new_string(observed->error.code));
            const TiredRecoveryArtifacts *artifacts = &files->artifacts[i];
            const TiredFileChange *change = &files->manifest.files[i];
            if (ok)
                ok = add(row, "staging",
                         artifact_json(&artifacts->staging, &artifacts->staging_error,
                                       change->after.exists)) &&
                     add(row, "rollback",
                         artifact_json(&artifacts->rollback, &artifacts->rollback_error,
                                       change->before.exists));
            if (!ok || json_object_array_add(rows, row) != 0)
            {
                json_object_put(row);
                goto fail;
            }
        }
    bool inserted = add(result, "destinations", rows);
    rows = NULL;
    if (!inserted)
        goto fail;
    return result;
fail:
    json_object_put(rows);
    json_object_put(result);
    return NULL;
}
/* Filesystem names need not be UTF-8. Keep the display ASCII and lossless even
 * for invalid byte sequences, controls and literal backslashes. */
static bool display_name(const TiredText *name, TiredText *output, TiredError *error)
{
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 1024);
    static const char hex[] = "0123456789abcdef";
    bool ok = true;
    for (size_t i = 0; ok && i < name->length; ++i)
    {
        unsigned char c = (unsigned char)name->data[i];
        if (c < 32 || c >= 127 || c == '\\')
        {
            char escaped[] = {'\\', 'x', hex[c >> 4], hex[c & 15]};
            ok = tired_buffer_append(&buffer, escaped, 4, error);
        }
        else
            ok = tired_buffer_append(&buffer, name->data + i, 1, error);
    }
    if (ok)
        ok = tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}
static bool eligible(const TiredTransactionInventoryEntry *entry)
{
    return entry->error.status == TIRED_OK && entry->progress.mode != TIRED_PROGRESS_COMMITTED &&
           entry->progress.mode != TIRED_PROGRESS_ROLLED_BACK;
}
static bool live_text(TiredBuffer *buffer, const TiredUnitBatchItem *item, TiredError *error)
{
    if (!item->query.done || item->query.error.status != TIRED_OK)
        return text(buffer, "  live: unknown; ", error) &&
               text(buffer,
                    item->query.error.message == NULL ? "Observation incomplete."
                                                      : item->query.error.message,
                    error) &&
               text(buffer, "\n", error);
    char stamp[64] = "timestamp unavailable";
    time_t seconds = (time_t)(item->completed_realtime_usec / 1000000);
    struct tm utc;
    if (gmtime_r(&seconds, &utc) != NULL)
        (void)strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
    if (!text(buffer, "  live (", error) || !text(buffer, stamp, error) ||
        !text(buffer, "): object=", error) ||
        !text(buffer, item->query.object_found ? "present" : "absent", error))
        return false;
    TiredText file_state = {0};
    const char *state = item->query.file_state == NULL ? "not_found" : item->query.file_state;
    bool state_ok = tired_encode_display(state, strlen(state), &file_state, error) &&
                    text(buffer, " file_state=", error) && text(buffer, file_state.data, error);
    tired_text_destroy(&file_state);
    if (!state_ok)
        return false;
    const TiredObservationId ids[] = {TIRED_OBS_ACTIVE_STATE, TIRED_OBS_SUB_STATE};
    const char *labels[] = {" active=", " sub="};
    for (size_t i = 0; i < 2; ++i)
    {
        if (!text(buffer, labels[i], error))
            return false;
        const TiredObservedValue *field =
            item->query.observation == NULL ? NULL : &item->query.observation->fields[ids[i]];
        if (field == NULL || !field->known)
        {
            if (!text(buffer, "unknown", error))
                return false;
        }
        else
        {
            TiredText escaped = {0};
            bool ok = tired_encode_display(field->value.text.data, field->value.text.length,
                                           &escaped, error) &&
                      text(buffer, escaped.data, error);
            tired_text_destroy(&escaped);
            if (!ok)
                return false;
        }
    }
    return text(buffer, "\n", error);
}
bool tired_recover_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                           TiredError *error)
{
    assert(request != NULL && output != NULL && result != NULL);
    if (request->command != TIRED_COMMAND_RECOVER || request->arguments.count != 0)
        return tired_error_set(error, TIRED_INVALID, "recover-command",
                               "Expected recover without resolution operands.", 0);
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    TiredLayout layout = {0};
    TiredTransactionInventory inventory = {0};
    TiredRecoveryLive live = {0};
    TiredRecoveryFiles files = {0};
    size_t file_budget = 64U * 1024U * 1024U;
    TiredTextList live_names = {0};
    TiredText name = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 4U * TIRED_INPUT_LIMIT);
    struct json_object *document = NULL, *entries = NULL, *row = NULL;
    bool ok = false, required = false;
    if (!tired_layout_discover(user, &layout, error) ||
        !tired_transaction_inventory_load(&layout, &inventory, error))
        goto done;
    required = !inventory.complete;
    for (size_t i = 0; i < inventory.count; ++i)
    {
        const TiredTransactionInventoryEntry *entry = &inventory.entries[i];
        if (!eligible(entry))
            continue;
        if (!tired_text_list_append(&live_names, entry->unit_name.data, entry->unit_name.length,
                                    2048, TIRED_INPUT_LIMIT, error) ||
            (entry->previous_unit_name.data != NULL &&
             !tired_text_list_append(&live_names, entry->previous_unit_name.data,
                                     entry->previous_unit_name.length, 2048, TIRED_INPUT_LIMIT,
                                     error)))
            goto done;
    }
    tired_recover_live_collect(user, &live_names, &live);
    size_t successful = 0, live_index = 0;
    for (size_t i = 0; i < live_names.count; ++i)
    {
        TiredUnitBatchItem item = tired_recover_live_item(&live, i);
        if (item.query.done && item.query.error.status == TIRED_OK)
            ++successful;
    }
    if (request->json)
    {
        document = json_object_new_object();
        entries = json_object_new_array();
        if (document == NULL || entries == NULL)
            goto allocation;
    }
    else if (!text(&buffer, "Transaction inspection (", error) ||
             !text(&buffer, user ? "user" : "system", error) ||
             !text(&buffer,
                   "). Recovery reconciliation incomplete; resolution actions are not yet "
                   "implemented.\n",
                   error))
        goto done;
    static const char *const modes[] = {"empty", "forward", "rollback", "committed", "rolled_back"};
    for (size_t i = 0; i < inventory.count; ++i)
    {
        const TiredTransactionInventoryEntry *entry = &inventory.entries[i];
        bool observe = eligible(entry);
        if (observe)
            tired_recover_files_collect(&layout, entry, &file_budget, &files);
        TiredUnitBatchItem observed = {0};
        if (observe)
            observed = tired_recover_live_item(&live, live_index++);
        bool observe_previous = observe && entry->previous_unit_name.data != NULL;
        TiredUnitBatchItem previous = {0};
        if (observe_previous)
            previous = tired_recover_live_item(&live, live_index++);
        bool known = entry->error.status == TIRED_OK;
        if (!known || (entry->progress.mode != TIRED_PROGRESS_COMMITTED &&
                       entry->progress.mode != TIRED_PROGRESS_ROLLED_BACK))
            required = true;
        const char *mode = known ? modes[entry->progress.mode] : "unknown";
        const char *pending = known && entry->progress.pending
                                  ? tired_transaction_action_name(entry->progress.pending_action)
                                  : "none";
        if (!display_name(&entry->directory_name, &name, error))
            goto done;
        if (request->json)
        {
            row = json_object_new_object();
            if (row == NULL ||
                !add(row, "directory_name_display", json_object_new_string(name.data)) ||
                !add(row, "journal_state", json_object_new_string(mode)) ||
                !add(row, "pending_action", json_object_new_string(known ? pending : "unknown")))
                goto allocation;
            if (entry->unit_name.data != NULL &&
                !add(row, "unit_name", json_object_new_string(entry->unit_name.data)))
                goto allocation;
            if (observe && !add(row, "live", tired_recover_live_json(&observed)))
                goto allocation;
            if (observe && !add(row, "files", file_json(&files)))
                goto allocation;
            if (observe_previous &&
                (!add(row, "previous_unit_name",
                      json_object_new_string(entry->previous_unit_name.data)) ||
                 !add(row, "previous_live", tired_recover_live_json(&previous))))
                goto allocation;
            if (!observe && !add(row, "live_status", json_object_new_string("not_requested")))
                goto allocation;
            if (known)
            {
                if (!add(row, "transaction_uuid",
                         json_object_new_string(entry->directory_name.data)) ||
                    !add(row, "sequence", json_object_new_uint64(entry->progress.sequence)) ||
                    !add(row, "explicit_uncertainty",
                         json_object_new_boolean(entry->progress.uncertain)) ||
                    !add(row, "had_failure", json_object_new_boolean(entry->progress.had_failure)))
                    goto allocation;
            }
            else if (!add(row, "error",
                          json_object_new_string(entry->error.code == NULL ? "unknown"
                                                                           : entry->error.code)) ||
                     !add(row, "message",
                          json_object_new_string(entry->error.message == NULL
                                                     ? "Inspection incomplete."
                                                     : entry->error.message)))
                goto allocation;
            if (json_object_array_add(entries, row) != 0)
                goto allocation;
            row = NULL;
        }
        else
        {
            if (!text(&buffer, name.data, error) || !text(&buffer, "  journal=", error) ||
                !text(&buffer, mode, error) || !text(&buffer, "  unit=", error) ||
                !text(&buffer, entry->unit_name.data == NULL ? "unknown" : entry->unit_name.data,
                      error))
                goto done;
            if (known)
            {
                char detail[160];
                (void)snprintf(detail, sizeof(detail), "  sequence=%" PRIu64 "  pending=%s%s\n",
                               entry->progress.sequence, pending,
                               entry->progress.uncertain ? " (uncertain)" : "");
                if (!text(&buffer, detail, error))
                    goto done;
            }
            else if (!text(&buffer, "\n  ", error) ||
                     !text(&buffer,
                           entry->error.message == NULL ? "Inspection incomplete."
                                                        : entry->error.message,
                           error) ||
                     !text(&buffer, "\n", error))
                goto done;
            if (observe && !live_text(&buffer, &observed, error))
                goto done;
            if (observe)
            {
                if (files.error.status != TIRED_OK)
                {
                    if (!text(&buffer, "  files=unknown: ", error) ||
                        !text(&buffer, files.error.message, error) || !text(&buffer, "\n", error))
                        goto done;
                }
                else
                    for (size_t j = 0; j < files.observations.count; ++j)
                    {
                        char line[160];
                        const TiredFileChange *change = &files.manifest.files[j];
                        const TiredRecoveryArtifacts *artifacts = &files.artifacts[j];
                        (void)snprintf(
                            line, sizeof(line), "  file[%zu]=%s staging=%s rollback=%s\n", j,
                            file_states[files.observations.files[j].state],
                            change->after.exists ? artifact_states[artifacts->staging.state]
                                                 : "not_required",
                            change->before.exists ? artifact_states[artifacts->rollback.state]
                                                  : "not_required");
                        if (!text(&buffer, line, error))
                            goto done;
                    }
            }
            if (observe_previous &&
                (!text(&buffer, "  previous_unit=", error) ||
                 !text(&buffer, entry->previous_unit_name.data, error) ||
                 !text(&buffer, "\n", error) || !live_text(&buffer, &previous, error)))
                goto done;
        }
    }
    TiredStatus status = required ? TIRED_RECOVERY_REQUIRED : TIRED_OK;
    if (request->json)
    {
        bool inserted = add(document, "transactions", entries);
        entries = NULL;
        if (!inserted || !add(document, "schema_version", json_object_new_int(1)) ||
            !add(document, "command", json_object_new_string("recover")) ||
            !add(document, "scope", json_object_new_string(user ? "user" : "system")) ||
            !add(document, "ok", json_object_new_boolean(!required)) ||
            !add(document, "exit_code", json_object_new_int(status)) ||
            !add(document, "inventory_complete", json_object_new_boolean(inventory.complete)) ||
            !add(document, "recovery_required", json_object_new_boolean(required)) ||
            !add(document, "live_requested_units", json_object_new_uint64(live_names.count)) ||
            !add(document, "live_successful_units", json_object_new_uint64(successful)) ||
            !add(document, "live_observations",
                 json_object_new_string(live_names.count == 0            ? "not_needed"
                                        : successful == live_names.count ? "completed"
                                        : successful == 0                ? "unavailable"
                                                                         : "partial")) ||
            !add(document, "live_reconciliation", json_object_new_string("not_performed")) ||
            !add(document, "resolution_actions_supported", json_object_new_boolean(false)))
            goto allocation;
        const char *serialized = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PRETTY);
        if (serialized == NULL)
            goto allocation;
        if (!text(&buffer, serialized, error) || !text(&buffer, "\n", error))
            goto done;
    }
    else if (!text(&buffer,
                   required ? "Recovery inspection required before conflicting mutations.\n"
                            : "No pending transactions found in stored journals.\n",
                   error))
        goto done;
    if (!tired_buffer_take(&buffer, output, error))
        goto done;
    *result = status;
    tired_error_clear(error);
    ok = true;
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation",
                    "Cannot allocate recovery inspection output.", 0);
done:
    tired_recover_files_destroy(&files);
    tired_recover_live_destroy(&live);
    tired_text_list_destroy(&live_names);
    json_object_put(row);
    json_object_put(entries);
    json_object_put(document);
    tired_text_destroy(&name);
    tired_buffer_destroy(&buffer);
    tired_transaction_inventory_destroy(&inventory);
    tired_layout_destroy(&layout);
    return ok;
}
