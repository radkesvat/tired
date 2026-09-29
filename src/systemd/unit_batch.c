#include "tired/unit_batch.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    TiredUnitQuery *query;
    bool attempted;
    uint64_t observed;
} Item;
struct TiredUnitBatch
{
    TiredManagerIdentity *identity;
    TiredTextList bases;
    Item *items;
    size_t next;
    uint64_t deadline;
    bool done;
    TiredError error;
};
static bool clock_usec(clockid_t clock, uint64_t *output)
{
    struct timespec now;
    if (clock_gettime(clock, &now) != 0 || now.tv_sec < 0)
        return false;
    *output = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
    return true;
}
static void stop(TiredUnitBatch *batch)
{
    if (batch->next < batch->bases.count)
    {
        tired_unit_query_destroy(batch->items[batch->next].query);
        batch->items[batch->next].query = NULL;
    }
    batch->done = true;
}
static bool remaining(TiredUnitBatch *batch, unsigned *milliseconds)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(batch->identity);
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        batch->error = owner.error;
        if (batch->error.status == TIRED_OK)
            tired_error_set(&batch->error, TIRED_CONFLICT, "unit-batch-owner",
                            "Manager identity is no longer ready.", 0);
        stop(batch);
        return false;
    }
    uint64_t now;
    if (!clock_usec(CLOCK_MONOTONIC, &now))
        tired_error_set(&batch->error, TIRED_INTERNAL, "unit-batch-clock",
                        "Cannot read unit batch clock.", errno);
    else if (now >= batch->deadline)
        tired_error_set(&batch->error, TIRED_RUNTIME_FAILED, "unit-batch-timeout",
                        "Unit observation batch timed out.", 0);
    else
    {
        *milliseconds = (unsigned)((batch->deadline - now + 999) / 1000);
        return true;
    }
    stop(batch);
    return false;
}
static bool queue(TiredUnitBatch *batch)
{
    unsigned milliseconds;
    if (!remaining(batch, &milliseconds))
        return false;
    Item *item = &batch->items[batch->next];
    if (!tired_unit_query_start(batch->identity, &batch->bases.items[batch->next], milliseconds,
                                &item->query, &batch->error))
    {
        stop(batch);
        return false;
    }
    item->attempted = true;
    return true;
}
bool tired_unit_batch_start(TiredManagerIdentity *identity, const TiredTextList *names,
                            unsigned timeout_ms, TiredUnitBatch **output, TiredError *error)
{
    assert(identity != NULL && names != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.kind != TIRED_MANAGER_SYSTEMD || owner.error.status != TIRED_OK ||
        names->count > 3072 || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(error, TIRED_INVALID, "unit-batch-input",
                               "Invalid unit batch input or manager identity.", 0);
    TiredUnitBatch *batch = calloc(1, sizeof(*batch));
    if (batch == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate unit batch.",
                               errno);
    batch->identity = identity;
    if (!clock_usec(CLOCK_MONOTONIC, &batch->deadline))
    {
        tired_error_set(error, TIRED_INTERNAL, "unit-batch-clock", "Cannot read unit batch clock.",
                        errno);
        goto failed;
    }
    batch->deadline += (uint64_t)timeout_ms * 1000;
    for (size_t i = 0; i < names->count; ++i)
    {
        const TiredText *name = &names->items[i];
        if (name->data == NULL || name->length <= 8 ||
            name->length > TIRED_EXPLICIT_NAME_LIMIT + 8 ||
            memcmp(name->data + name->length - 8, ".service", 8) != 0 ||
            !tired_name_validate_base(name->data, name->length - 8, error))
        {
            tired_error_set(error, TIRED_INVALID, "unit-batch-name",
                            "Expected full safe service names.", 0);
            goto failed;
        }
        if (!tired_text_list_append(&batch->bases, name->data, name->length - 8, 3072,
                                    TIRED_INPUT_LIMIT, error))
            goto failed;
    }
    if (names->count != 0)
    {
        batch->items = calloc(names->count, sizeof(*batch->items));
        if (batch->items == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation",
                            "Cannot allocate unit batch items.", errno);
            goto failed;
        }
        if (!queue(batch))
        {
            if (error != NULL)
                *error = batch->error;
            goto failed;
        }
    }
    else
        batch->done = true;
    *output = batch;
    tired_error_clear(error);
    return true;
failed:
    tired_unit_batch_destroy(batch);
    return false;
}
bool tired_unit_batch_step(TiredUnitBatch *batch)
{
    assert(batch != NULL);
    unsigned milliseconds;
    if (batch->done || !remaining(batch, &milliseconds))
        return true;
    Item *item = &batch->items[batch->next];
    if (!tired_unit_query_step(item->query))
        return false;
    if (!remaining(batch, &milliseconds))
        return true;
    if (!clock_usec(CLOCK_REALTIME, &item->observed))
    {
        tired_error_set(&batch->error, TIRED_INTERNAL, "unit-batch-clock",
                        "Cannot timestamp unit observation.", errno);
        stop(batch);
        return true;
    }
    ++batch->next;
    if (batch->next == batch->bases.count)
        batch->done = true;
    else
        (void)queue(batch);
    return batch->done;
}
bool tired_unit_batch_poll(TiredUnitBatch *batch, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error)
{
    assert(batch != NULL && descriptor != NULL && deadline_usec != NULL);
    if (batch->done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    if (!tired_unit_query_poll(batch->items[batch->next].query, descriptor, deadline_usec, error))
        return false;
    if (batch->deadline < *deadline_usec)
        *deadline_usec = batch->deadline;
    return true;
}
void tired_unit_batch_cancel(TiredUnitBatch *batch)
{
    assert(batch != NULL);
    if (!batch->done)
    {
        tired_error_set(&batch->error, TIRED_CANCELLED, "unit-batch-cancelled",
                        "Unit observation batch was cancelled.", 0);
        stop(batch);
    }
}
TiredUnitBatchResult tired_unit_batch_result(const TiredUnitBatch *batch)
{
    assert(batch != NULL);
    TiredUnitBatchResult result = {
        .done = batch->done, .count = batch->bases.count, .error = batch->error};
    TiredManagerIdentityResult owner = tired_manager_identity_result(batch->identity);
    if (result.error.status == TIRED_OK && owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
    }
    return result;
}
TiredUnitBatchItem tired_unit_batch_item(const TiredUnitBatch *batch, size_t index)
{
    assert(batch != NULL && index < batch->bases.count);
    TiredUnitBatchItem item = {.attempted = batch->items[index].attempted};
    if (index < batch->next)
    {
        item.query = tired_unit_query_result(batch->items[index].query);
        item.completed_realtime_usec = batch->items[index].observed;
    }
    else
    {
        TiredUnitBatchResult result = tired_unit_batch_result(batch);
        item.query.done = result.done;
        item.query.error = result.error;
    }
    return item;
}
void tired_unit_batch_destroy(TiredUnitBatch *batch)
{
    if (batch == NULL)
        return;
    if (batch->items != NULL)
        for (size_t i = 0; i < batch->bases.count; ++i)
            tired_unit_query_destroy(batch->items[i].query);
    free(batch->items);
    tired_text_list_destroy(&batch->bases);
    free(batch);
}
