#include "inspection_live.h"
#include "tired/name.h"
#include "tired/service_inventory.h"
#include "tired/status_frontend.h"
#include "tired/transaction_inventory.h"
#include <assert.h>
#include <string.h>

bool tired_status_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                          TiredError *error)
{
    assert(request != NULL && output != NULL && result != NULL);
    if (request->command != TIRED_COMMAND_STATUS || request->arguments.count != 1)
        return tired_error_set(error, TIRED_INVALID, "status-command", "Expected status NAME.", 0);
    TiredText base = {0}, unit = {0};
    TiredLayout layout = {0};
    TiredServiceInventory inventory = {0};
    TiredTransactionInventory transactions = {0};
    TiredServiceRecord record = {0};
    TiredInspectionLive live = {0};
    TiredTextList names = {0};
    TiredStatusView view = {
        .user_scope = tired_spec_choice_is(&request->overrides, TIRED_FIELD_SCOPE, "user")};
    const TiredText *argument = &request->arguments.items[0];
    bool ok = false;
    if (!tired_name_explicit(argument->data, argument->length, &base, error) ||
        !tired_name_candidate(&base, 1, &unit, error) ||
        !tired_layout_discover(view.user_scope, &layout, error) ||
        !tired_text_list_append(&names, unit.data, unit.length, 1, 256, error))
        goto done;
    view.unit_name = unit.data;
    const TiredServiceMetadata *metadata = NULL;
    if (tired_service_inventory_load(&layout, &inventory, &view.record_error) &&
        tired_service_inventory_find(&inventory, unit.data, &metadata, &view.record_error) &&
        tired_service_record_load(&layout, metadata->service_uuid, &record, &view.record_error))
    {
        /* A rename between inventory and reload must not select a different unit. */
        if (strcmp(record.metadata.unit_name.data, unit.data) != 0)
            tired_error_set(&view.record_error, TIRED_CONFLICT, "status-record-changed",
                            "Selected record changed during inspection.", 0);
        else
        {
            view.record = &record;
            TiredError files_error = {0};
            if (!tired_service_files_inspect(&layout, &record, &view.files, &files_error))
                view.files.unit.error = view.files.environment.error = files_error;
        }
    }
    if (tired_transaction_inventory_load(&layout, &transactions, &view.transactions_error))
    {
        const TiredTextList *pending = NULL;
        if (tired_transaction_inventory_pending(&transactions, &pending, &view.transactions_error))
            for (size_t i = 0; i < pending->count; ++i)
                view.transaction_pending |= strcmp(pending->items[i].data, unit.data) == 0;
    }
    tired_inspection_live_collect(view.user_scope, &names, &live);
    view.live = tired_inspection_live_item(&live, 0);
    if (view.user_scope)
        tired_linger_observe(2000, &view.linger);
    ok = tired_status_output(&view, request->json, request->check_active, output, result, error);
done:
    tired_inspection_live_destroy(&live);
    tired_text_list_destroy(&names);
    tired_service_record_destroy(&record);
    tired_transaction_inventory_destroy(&transactions);
    tired_service_inventory_destroy(&inventory);
    tired_layout_destroy(&layout);
    tired_text_destroy(&base);
    tired_text_destroy(&unit);
    return ok;
}
