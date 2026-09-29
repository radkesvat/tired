#include "tired/encode.h"
#include "tired/recover_frontend.h"
#include "tired/transaction_inventory.h"
#include <assert.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

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
    TiredText name = {0};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 4U * TIRED_INPUT_LIMIT);
    struct json_object *document = NULL, *entries = NULL, *row = NULL;
    bool ok = false, required = false;
    if (!tired_layout_discover(user, &layout, error) ||
        !tired_transaction_inventory_load(&layout, &inventory, error))
        goto done;
    required = !inventory.complete;
    if (request->json)
    {
        document = json_object_new_object();
        entries = json_object_new_array();
        if (document == NULL || entries == NULL)
            goto allocation;
    }
    else if (!text(&buffer, "Stored transaction inspection (", error) ||
             !text(&buffer, user ? "user" : "system", error) ||
             !text(&buffer,
                   "). Live reconciliation not performed; resolution actions are not yet "
                   "implemented.\n",
                   error))
        goto done;
    static const char *const modes[] = {"empty", "forward", "rollback", "committed", "rolled_back"};
    for (size_t i = 0; i < inventory.count; ++i)
    {
        const TiredTransactionInventoryEntry *entry = &inventory.entries[i];
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
    json_object_put(row);
    json_object_put(entries);
    json_object_put(document);
    tired_text_destroy(&name);
    tired_buffer_destroy(&buffer);
    tired_transaction_inventory_destroy(&inventory);
    tired_layout_destroy(&layout);
    return ok;
}
