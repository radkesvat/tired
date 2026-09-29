#ifndef TIRED_UNIT_BATCH_H
#define TIRED_UNIT_BATCH_H
#include "tired/unit_query.h"
typedef struct TiredUnitBatch TiredUnitBatch;
typedef struct
{
    bool done;
    size_t count;
    TiredError error; /* Batch admission/transport/deadline error; inspect each item too. */
} TiredUnitBatchResult;
typedef struct
{
    bool attempted;
    uint64_t completed_realtime_usec; /* Includes error replies; zero if not completed. */
    TiredUnitQueryResult query;
} TiredUnitBatchItem;
/* Copy <=3072 full safe service names, preserving order/duplicates. Sequential
 * read-only observations share one overall deadline. Identity must outlive batch.
 * A per-unit error is retained and does not hide unrelated successful units.
 * Owner invalidation hides all successful snapshots. No activation or mutation. */
bool tired_unit_batch_start(TiredManagerIdentity *identity, const TiredTextList *names,
                            unsigned timeout_ms, TiredUnitBatch **batch, TiredError *error);
bool tired_unit_batch_step(TiredUnitBatch *batch);
bool tired_unit_batch_poll(TiredUnitBatch *batch, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error);
void tired_unit_batch_cancel(TiredUnitBatch *batch);
TiredUnitBatchResult tired_unit_batch_result(const TiredUnitBatch *batch);
/* Borrowed item data, valid until destruction. Check query.done/error before
 * interpreting any presence/state fields. Not an atomic multi-unit snapshot. */
TiredUnitBatchItem tired_unit_batch_item(const TiredUnitBatch *batch, size_t index);
void tired_unit_batch_destroy(TiredUnitBatch *batch);
#endif
