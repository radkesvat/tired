#include "inspection_live.h"
#include "tired/encode.h"
#include "tired/json.h"
#include "tired/list_frontend.h"
#include "tired/service_inventory.h"
#include "tired/status_frontend.h"
#include "tired/transaction_inventory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
static bool contains(const TiredTextList *names, const char *name)
{
    for (size_t i = 0; i < names->count; ++i)
        if (strcmp(names->items[i].data, name) == 0)
            return true;
    return false;
}
/* Filesystem entries can contain arbitrary bytes, including invalid UTF-8. */
static bool filename(const TiredText *name, TiredText *output, TiredError *error)
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
static struct json_object *member(struct json_object *object, const char *key)
{
    struct json_object *value = NULL;
    if (object != NULL)
        (void)json_object_object_get_ex(object, key, &value);
    return value;
}
static const char *string_member(struct json_object *object, const char *key)
{
    struct json_object *value = member(object, key);
    return value == NULL ? NULL : json_object_get_string(value);
}
static bool filter_rows(const TiredRequest *request, struct json_object **rows, size_t *excluded,
                        size_t *uncertain)
{
    struct json_object *selected = json_object_new_array();
    if (selected == NULL)
        return false;
    for (size_t i = 0; i < json_object_array_length(*rows); ++i)
    {
        struct json_object *row = json_object_array_get_idx(*rows, i);
        struct json_object *live = member(row, "live"), *properties = member(live, "properties");
        const char *active = string_member(properties, "ActiveState");
        const char *enabled = string_member(live, "file_state");
        struct json_object *object_found = member(live, "object_found"),
                           *file_found = member(live, "file_found");
        if (object_found != NULL && !json_object_get_boolean(object_found))
            active = "not-loaded";
        if (file_found != NULL && !json_object_get_boolean(file_found))
            enabled = "not-found";
        TiredListMatch match = tired_list_match(request, string_member(row, "unit_name"), active,
                                                enabled, string_member(row, "profile"));
        if (match == TIRED_LIST_NO_MATCH)
        {
            ++*excluded;
            continue;
        }
        if (match == TIRED_LIST_UNKNOWN)
            ++*uncertain;
        if (!add(row, "filter_match",
                 json_object_new_string(match == TIRED_LIST_UNKNOWN ? "unknown" : "matched")))
            goto fail;
        struct json_object *owned = json_object_get(row);
        if (json_object_array_add(selected, owned) != 0)
        {
            json_object_put(owned);
            goto fail;
        }
    }
    json_object_put(*rows);
    *rows = selected;
    return true;
fail:
    json_object_put(selected);
    return false;
}
static bool cell(TiredBuffer *buffer, struct json_object *value, TiredError *error)
{
    const char *raw = value == NULL ? "unknown" : json_object_get_string(value);
    TiredText safe = {0};
    bool ok =
        tired_encode_display(raw, strlen(raw), &safe, error) && text(buffer, safe.data, error);
    tired_text_destroy(&safe);
    return ok;
}
static bool row_text(TiredBuffer *buffer, struct json_object *row, TiredError *error)
{
    struct json_object *live = member(row, "live"), *properties = member(live, "properties");
    struct json_object *name = member(row, "unit_name");
    if (name == NULL)
        name = member(row, "filename_display");
    struct json_object *values[] = {name,
                                    member(row, "scope"),
                                    member(properties, "ActiveState"),
                                    member(properties, "SubState"),
                                    member(live, "file_state"),
                                    member(row, "service_uid"),
                                    member(row, "profile"),
                                    member(member(row, "unit_file"), "state"),
                                    member(member(row, "environment_file"), "state"),
                                    member(row, "transactions")};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if ((i != 0 && !text(buffer, "  ", error)) ||
            !(i == 0 && member(row, "unit_name") == NULL
                  ? text(buffer, json_object_get_string(name), error)
                  : cell(buffer, values[i], error)))
            return false;
    const char *keys[] = {"record_error", "error"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        struct json_object *value = member(row, keys[i]);
        if (value != NULL && (!text(buffer, "  ", error) || !cell(buffer, value, error)))
            return false;
    }
    struct json_object *dropins = member(properties, "DropInPaths");
    if (dropins != NULL && json_object_array_length(dropins) != 0 &&
        !text(buffer, "  drop-ins=present", error))
        return false;
    const char *match = string_member(row, "filter_match");
    if (match != NULL && strcmp(match, "unknown") == 0 && !text(buffer, "  filter=unknown", error))
        return false;
    return text(buffer, "\n", error);
}
bool tired_list_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                        TiredError *error)
{
    assert(request != NULL && output != NULL && result != NULL);
    if (request->command != TIRED_COMMAND_LIST || request->arguments.count != 0)
        return tired_error_set(error, TIRED_INVALID, "list-command",
                               "Expected list without operands.", 0);
    bool user = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user");
    TiredLayout layout = {0};
    TiredServiceInventory inventory = {0};
    TiredTransactionInventory transactions = {0};
    TiredError tx_error = {0}, directory_error = {0};
    TiredDirectory *directory = NULL;
    TiredInspectionLive live = {0};
    TiredTextList names = {0};
    TiredText encoded = {0}, display = {0};
    TiredServiceRecord record = {0};
    struct json_object *document = NULL, *rows = NULL, *row = NULL;
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 8U * TIRED_INPUT_LIMIT);
    size_t record_budget = 64U * TIRED_INPUT_LIMIT, file_budget = 64U * TIRED_INPUT_LIMIT;
    bool ok = false;
    TiredStatus status = TIRED_OK;
    if (!tired_layout_discover(user, &layout, error) ||
        !tired_service_inventory_load(&layout, &inventory, error))
        goto done;
    if (!tired_transaction_inventory_load(&layout, &transactions, &tx_error) ||
        !transactions.complete)
    {
        if (tx_error.status == TIRED_OK)
            tired_error_set(&tx_error, TIRED_RECOVERY_REQUIRED, "list-transactions-incomplete",
                            "Transaction inventory is incomplete.", 0);
        status = TIRED_RECOVERY_REQUIRED;
    }
    if (!inventory.complete)
        status = TIRED_RECOVERY_REQUIRED;
    for (size_t i = 0; i < inventory.count; ++i)
    {
        const TiredText *unit = &inventory.entries[i].metadata.unit_name;
        if (unit->data != NULL && !tired_text_list_append(&names, unit->data, unit->length, 1024,
                                                          TIRED_INPUT_LIMIT, error))
            goto done;
    }
    size_t record_names = names.count;
    for (size_t i = 0; i < transactions.pending_names.count; ++i)
    {
        const TiredText *name = &transactions.pending_names.items[i];
        if (!contains(&names, name->data) &&
            !tired_text_list_append(&names, name->data, name->length, 3072, TIRED_INPUT_LIMIT,
                                    error))
            goto done;
    }
    if (record_names != 0)
        (void)tired_directory_open(layout.paths[TIRED_PATH_RECORDS].data, user ? geteuid() : 0,
                                   true, &directory, &directory_error);
    tired_inspection_live_collect(user, &names, &live);
    document = json_object_new_object();
    rows = json_object_new_array();
    if (document == NULL || rows == NULL)
        goto allocation;
    size_t live_index = 0;
    for (size_t i = 0; i < inventory.count; ++i)
    {
        const TiredServiceInventoryEntry *entry = &inventory.entries[i];
        tired_service_record_destroy(&record);
        if (!filename(&entry->filename, &display, error))
            goto done;
        if (entry->metadata.unit_name.data == NULL)
        {
            row = json_object_new_object();
            if (row == NULL || !add(row, "record", json_object_new_string("unknown")) ||
                !add(row, "scope", json_object_new_string(user ? "user" : "system")) ||
                !add(row, "error",
                     json_object_new_string(entry->error.code == NULL ? "invalid-record"
                                                                      : entry->error.code)))
                goto allocation;
        }
        else
        {
            TiredStatusView view = {.unit_name = entry->metadata.unit_name.data,
                                    .user_scope = user,
                                    .record_error = entry->error,
                                    .transactions_error = tx_error,
                                    .live = tired_inspection_live_item(&live, live_index++)};
            view.transaction_pending = contains(&transactions.pending_names, view.unit_name);
            if (view.record_error.status == TIRED_OK)
            {
                if (directory == NULL)
                    view.record_error = directory_error;
                else if (tired_service_record_read_budget(
                             directory, &layout, entry->metadata.service_uuid, &record_budget,
                             &record, &view.record_error))
                {
                    if (strcmp(record.metadata.unit_name.data, view.unit_name) != 0)
                        tired_error_set(&view.record_error, TIRED_CONFLICT, "list-record-changed",
                                        "Selected record changed during listing.", 0);
                    else
                    {
                        view.record = &record;
                        TiredError file_error = {0};
                        if (!tired_service_files_inspect_budget(&layout, &record, &file_budget,
                                                                &view.files, &file_error))
                            view.files.unit.error = view.files.environment.error = file_error;
                    }
                }
            }
            if (view.record_error.status != TIRED_OK || view.files.unit.error.status != TIRED_OK ||
                view.files.environment.error.status != TIRED_OK)
                status = TIRED_RECOVERY_REQUIRED;
            TiredStatus item_status;
            if (!tired_status_output(&view, true, false, &encoded, &item_status, error) ||
                !tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &row, error))
                goto done;
            if (status == TIRED_OK && item_status != TIRED_OK)
                status = item_status;
            json_object_object_del(row, "command");
            json_object_object_del(row, "schema_version");
            if (view.record != NULL &&
                !add(row, "profile",
                     json_object_new_string(record.has_profile ? record.profile.profile.id
                                                               : "none")))
                goto allocation;
        }
        if (!add(row, "filename_display", json_object_new_string(display.data)) ||
            json_object_array_add(rows, row) != 0)
            goto allocation;
        row = NULL;
    }
    /* Pending create/rename/remove reservations can outlive a current record. */
    for (size_t i = record_names; i < names.count; ++i)
    {
        TiredStatusView view = {.unit_name = names.items[i].data,
                                .user_scope = user,
                                .transaction_pending = true,
                                .transactions_error = tx_error,
                                .live = tired_inspection_live_item(&live, i)};
        tired_error_set(
            &view.record_error, inventory.complete ? TIRED_NOT_FOUND : TIRED_RECOVERY_REQUIRED,
            inventory.complete ? "service-record-not-found" : "service-inventory-incomplete",
            "No unambiguous current record was found for this transaction name.", 0);
        TiredStatus item_status;
        if (!tired_status_output(&view, true, false, &encoded, &item_status, error) ||
            !tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &row, error))
            goto done;
        json_object_object_del(row, "command");
        json_object_object_del(row, "schema_version");
        /* Absence during a pending create/remove is an observed state, not a
         * failed listing. Transport failures still make the report partial. */
        if (status == TIRED_OK && item_status != TIRED_OK && item_status != TIRED_NOT_FOUND)
            status = item_status;
        if (json_object_array_add(rows, row) != 0)
            goto allocation;
        row = NULL;
    }
    size_t excluded = 0, uncertain = 0;
    if (!filter_rows(request, &rows, &excluded, &uncertain))
        goto allocation;
    if (request->json)
    {
        bool inserted = add(document, "services", rows);
        rows = NULL;
        if (!inserted || !add(document, "schema_version", json_object_new_int(1)) ||
            !add(document, "command", json_object_new_string("list")) ||
            !add(document, "scope", json_object_new_string(user ? "user" : "system")) ||
            !add(document, "ok", json_object_new_boolean(status == TIRED_OK)) ||
            !add(document, "exit_code", json_object_new_int(status)) ||
            !add(document, "filtered_out", json_object_new_uint64(excluded)) ||
            !add(document, "filter_unknown", json_object_new_uint64(uncertain)) ||
            !add(document, "inventory_complete", json_object_new_boolean(inventory.complete)) ||
            !add(document, "transactions_complete",
                 json_object_new_boolean(tx_error.status == TIRED_OK)) ||
            (tx_error.code != NULL &&
             !add(document, "transactions_error", json_object_new_string(tx_error.code))))
            goto allocation;
        const char *serialized = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PRETTY);
        if (serialized == NULL)
            goto allocation;
        if (!text(&buffer, serialized, error) || !text(&buffer, "\n", error))
            goto done;
    }
    else
    {
        if (!text(&buffer,
                  "NAME  SCOPE  ACTIVE  SUBSTATE  ENABLEMENT  UID  PROFILE  UNIT  ENVIRONMENT  "
                  "TRANSACTIONS\n",
                  error))
            goto done;
        for (size_t i = 0; i < json_object_array_length(rows); ++i)
            if (!row_text(&buffer, json_object_array_get_idx(rows, i), error))
                goto done;
        if (json_object_array_length(rows) == 0 &&
            !text(&buffer,
                  excluded != 0 ? "No services match the requested filters.\n"
                                : "No managed services found.\n",
                  error))
            goto done;
        if (excluded != 0 || uncertain != 0)
        {
            char summary[128];
            (void)snprintf(summary, sizeof(summary),
                           "Filtered out %zu rows; %zu rows have unknown filter facts.\n", excluded,
                           uncertain);
            if (!text(&buffer, summary, error))
                goto done;
        }
        if (tx_error.message != NULL &&
            (!text(&buffer, tx_error.message, error) || !text(&buffer, "\n", error)))
            goto done;
        if (status != TIRED_OK &&
            !text(&buffer, "Some observations are incomplete; use --json for details.\n", error))
            goto done;
    }
    if (!tired_buffer_take(&buffer, output, error))
        goto done;
    *result = status;
    tired_error_clear(error);
    ok = true;
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate service listing.", 0);
done:
    json_object_put(row);
    json_object_put(rows);
    json_object_put(document);
    tired_buffer_destroy(&buffer);
    tired_service_record_destroy(&record);
    tired_text_destroy(&encoded);
    tired_text_destroy(&display);
    tired_text_list_destroy(&names);
    tired_inspection_live_destroy(&live);
    tired_directory_destroy(directory);
    tired_transaction_inventory_destroy(&transactions);
    tired_service_inventory_destroy(&inventory);
    tired_layout_destroy(&layout);
    return ok;
}
