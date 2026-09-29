#include "tired/name_query.h"
#include "tired/layout.h"
#include "tired/load_paths.h"
#include "tired/name_selection.h"
#include "tired/transaction_inventory.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct TiredNameQuery
{
    TiredManagerIdentity *identity;
    TiredLoadPaths *paths;
    TiredUnitQuery *unit;
    TiredNameSelection *selection;
    TiredText base, destination;
    TiredTextList pending;
    uint64_t deadline;
    bool explicit_name, done;
    TiredError error;
};
static bool now_usec(uint64_t *now)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *now = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void detach(TiredNameQuery *query)
{
    tired_load_paths_destroy(query->paths);
    query->paths = NULL;
    tired_unit_query_destroy(query->unit);
    query->unit = NULL;
}
static bool fail(TiredNameQuery *query, TiredStatus status, const char *code, const char *message)
{
    query->done = true;
    return tired_error_set(&query->error, status, code, message, 0);
}
static bool remaining(TiredNameQuery *query, unsigned *milliseconds)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        fail(query, TIRED_CONFLICT, "name-query-owner", "Manager identity is no longer ready.");
        if (owner.error.status != TIRED_OK)
            query->error = owner.error;
        return false;
    }
    uint64_t now;
    if (!now_usec(&now))
        return fail(query, TIRED_INTERNAL, "name-query-clock", "Cannot read name query clock.");
    if (now >= query->deadline)
        return fail(query, TIRED_RUNTIME_FAILED, "name-query-timeout", "Name discovery timed out.");
    *milliseconds = (unsigned)((query->deadline - now + 999) / 1000);
    return true;
}
static bool queue_candidate(TiredNameQuery *query)
{
    unsigned milliseconds;
    if (!remaining(query, &milliseconds))
        return false;
    const TiredText *name = tired_name_selection_candidate(query->selection);
    TiredText base = {0};
    bool ok =
        tired_text_set(&base, name->data, name->length - 8, 200, &query->error) &&
        tired_unit_query_start(query->identity, &base, milliseconds, &query->unit, &query->error);
    tired_text_destroy(&base);
    if (!ok)
        query->done = true;
    return ok;
}
bool tired_name_query_start(TiredManagerIdentity *identity, const TiredText *base,
                            bool explicit_name, const TiredText *destination,
                            const TiredTextList *pending_names, unsigned timeout_ms,
                            TiredNameQuery **output, TiredError *error)
{
    assert(identity != NULL && base != NULL && destination != NULL && pending_names != NULL);
    assert(output != NULL && *output == NULL);
    if (timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(error, TIRED_INVALID, "name-query-input",
                               "Name query requires a bounded positive deadline.", 0);
    TiredNameQuery *query = calloc(1, sizeof(*query));
    if (query == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate name query.",
                               errno);
    query->identity = identity;
    query->explicit_name = explicit_name;
    if (!now_usec(&query->deadline))
    {
        tired_error_set(error, TIRED_INTERNAL, "name-query-clock", "Cannot read name query clock.",
                        errno);
        goto failed;
    }
    query->deadline += (uint64_t)timeout_ms * 1000;
    if (!tired_text_set(&query->base, base->data, base->length, 200, error) ||
        !tired_text_set(&query->destination, destination->data, destination->length, 4096, error))
        goto failed;
    for (size_t i = 0; i < pending_names->count; ++i)
        if (!tired_text_list_append(&query->pending, pending_names->items[i].data,
                                    pending_names->items[i].length, 4096, TIRED_INPUT_LIMIT, error))
            goto failed;
    /* Validate caller inputs before queuing any manager request. Rebuild with
     * discovered locations when UnitPath arrives. */
    TiredTextList destination_only = {.items = &query->destination, .count = 1};
    if (!tired_name_selection_start(&query->base, explicit_name, &destination_only, &query->pending,
                                    &query->selection, error))
        goto failed;
    unsigned milliseconds;
    if (!remaining(query, &milliseconds))
    {
        if (error != NULL)
            *error = query->error;
        goto failed;
    }
    if (!tired_load_paths_start(identity, milliseconds, &query->paths, error))
        goto failed;
    *output = query;
    tired_error_clear(error);
    return true;
failed:
    tired_name_query_destroy(query);
    return false;
}
static bool prepare_selection(TiredNameQuery *query, const TiredTextList *paths)
{
    TiredLayout layout = {.user_scope = tired_manager_identity_result(query->identity).user_scope};
    /* Borrow the destination only for comparison; layout owns no copied values. */
    layout.paths[TIRED_PATH_UNITS] = query->destination;
    if (!tired_layout_check_unit_path(&layout, paths, &query->error))
        return false;
    TiredTextList locations = {0};
    bool included = false, ok = false;
    for (size_t i = 0; i < paths->count; ++i)
    {
        const TiredText *path = &paths->items[i];
        if (!tired_text_list_append(&locations, path->data, path->length, 256, TIRED_INPUT_LIMIT,
                                    &query->error))
            goto done;
        if (strcmp(path->data, query->destination.data) == 0)
            included = true;
    }
    if (!included &&
        !tired_text_list_append(&locations, query->destination.data, query->destination.length, 256,
                                TIRED_INPUT_LIMIT, &query->error))
        goto done;
    tired_name_selection_destroy(query->selection);
    query->selection = NULL;
    ok = tired_name_selection_start(&query->base, query->explicit_name, &locations, &query->pending,
                                    &query->selection, &query->error);
done:
    tired_text_list_destroy(&locations);
    return ok;
}
bool tired_name_query_step(TiredNameQuery *query)
{
    assert(query != NULL);
    unsigned milliseconds;
    if (query->done)
        return true;
    if (!remaining(query, &milliseconds))
        goto done;
    if (query->paths != NULL)
    {
        if (!tired_load_paths_step(query->paths))
            return false;
        TiredLoadPathsResult paths = tired_load_paths_result(query->paths);
        if (paths.error.status != TIRED_OK)
        {
            query->error = paths.error;
            query->done = true;
        }
        else if (!prepare_selection(query, paths.directories))
            query->done = true;
        tired_load_paths_destroy(query->paths);
        query->paths = NULL;
        if (!query->done)
            (void)queue_candidate(query);
    }
    else if (tired_unit_query_step(query->unit))
    {
        TiredUnitQueryResult unit = tired_unit_query_result(query->unit);
        bool available = false;
        if (unit.error.status != TIRED_OK)
        {
            query->error = unit.error;
            query->done = true;
        }
        else if (!tired_name_selection_observe(query->selection, &unit, &available, &query->error))
            query->done = true;
        tired_unit_query_destroy(query->unit);
        query->unit = NULL;
        if (!query->done && remaining(query, &milliseconds))
        {
            if (available)
                query->done = true;
            else
                (void)queue_candidate(query);
        }
    }
done:
    if (query->done)
        detach(query);
    return query->done;
}
bool tired_name_query_poll(TiredNameQuery *query, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error)
{
    assert(query != NULL && descriptor != NULL && deadline_usec != NULL);
    if (query->done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    bool ok = query->paths != NULL
                  ? tired_load_paths_poll(query->paths, descriptor, deadline_usec, error)
                  : tired_unit_query_poll(query->unit, descriptor, deadline_usec, error);
    if (ok && query->deadline < *deadline_usec)
        *deadline_usec = query->deadline;
    return ok;
}
void tired_name_query_cancel(TiredNameQuery *query)
{
    assert(query != NULL);
    if (!query->done)
    {
        fail(query, TIRED_CANCELLED, "name-query-cancelled", "Name discovery was cancelled.");
        detach(query);
    }
}
TiredNameQueryResult tired_name_query_result(const TiredNameQuery *query)
{
    assert(query != NULL);
    TiredNameQueryResult result = {.done = query->done, .error = query->error};
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    if (result.error.status == TIRED_OK && owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
    }
    if (result.done && result.error.status == TIRED_OK)
        result.unit_name = tired_name_selection_candidate(query->selection);
    return result;
}
void tired_name_query_destroy(TiredNameQuery *query)
{
    if (query == NULL)
        return;
    detach(query);
    tired_name_selection_destroy(query->selection);
    tired_text_destroy(&query->base);
    tired_text_destroy(&query->destination);
    tired_text_list_destroy(&query->pending);
    free(query);
}

bool tired_name_query_discover(TiredManagerIdentity *identity, const TiredText *base,
                               bool explicit_name, unsigned timeout_ms, TiredNameQuery **output,
                               TiredError *error)
{
    assert(identity != NULL && base != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.error.status != TIRED_OK || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(
            error, TIRED_INVALID, "name-discovery-input",
            "Name discovery requires a ready manager identity and bounded deadline.", 0);
    TiredLayout layout = {0};
    TiredTransactionInventory inventory = {0};
    const TiredTextList *pending = NULL;
    bool ok = tired_layout_discover(owner.user_scope, &layout, error) &&
              tired_transaction_inventory_load(&layout, &inventory, error) &&
              tired_transaction_inventory_pending(&inventory, &pending, error) &&
              tired_name_query_start(identity, base, explicit_name, &layout.paths[TIRED_PATH_UNITS],
                                     pending, timeout_ms, output, error);
    tired_transaction_inventory_destroy(&inventory);
    tired_layout_destroy(&layout);
    return ok;
}
