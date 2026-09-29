#include "tired/transaction_progress.h"
#include <assert.h>

static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_CONFLICT, "transaction-transition",
                           "Transaction progress has an invalid or unresolved action transition.",
                           0);
}
bool tired_transaction_progress_advance(TiredTransactionProgress *progress,
                                        const TiredTransactionRecord *record, TiredError *error)
{
    assert(progress != NULL && record != NULL);
    if (record->sequence == 0 || record->sequence > TIRED_TRANSACTION_SEQUENCE_LIMIT ||
        record->sequence != progress->sequence + 1 ||
        (unsigned)record->action >= TIRED_ACTION_COUNT ||
        (unsigned)record->state >= TIRED_ACTION_STATE_COUNT ||
        progress->mode == TIRED_PROGRESS_COMMITTED || progress->mode == TIRED_PROGRESS_ROLLED_BACK)
        return invalid(error);
    TiredTransactionProgress next = *progress;
    if (next.mode == TIRED_PROGRESS_EMPTY)
    {
        if (record->sequence != 1 || record->action != TIRED_ACTION_PREPARE ||
            record->state != TIRED_ACTION_COMPLETED)
            return invalid(error);
        next.mode = TIRED_PROGRESS_FORWARD;
    }
    else if (record->action == TIRED_ACTION_PREPARE)
        return invalid(error);
    else if (next.pending)
    {
        if (record->action != next.pending_action || record->state == TIRED_ACTION_INTENT)
            return invalid(error);
        if (record->state == TIRED_ACTION_UNCERTAIN)
            next.uncertain = true;
        else
        {
            next.pending = next.uncertain = false;
            next.had_failure |= record->state == TIRED_ACTION_FAILED;
            if (record->action == TIRED_ACTION_COMMIT && record->state == TIRED_ACTION_COMPLETED)
                next.mode = TIRED_PROGRESS_COMMITTED;
        }
    }
    else if (record->action == TIRED_ACTION_ROLLBACK)
    {
        if (next.mode == TIRED_PROGRESS_FORWARD && record->state == TIRED_ACTION_INTENT)
            next.mode = TIRED_PROGRESS_ROLLBACK;
        else if (next.mode == TIRED_PROGRESS_ROLLBACK && record->state == TIRED_ACTION_COMPLETED)
            next.mode = TIRED_PROGRESS_ROLLED_BACK;
        else
            return invalid(error);
    }
    else
    {
        if (record->state != TIRED_ACTION_INTENT ||
            (next.mode == TIRED_PROGRESS_ROLLBACK && record->action == TIRED_ACTION_COMMIT))
            return invalid(error);
        next.pending = true;
        next.uncertain = false;
        next.pending_action = record->action;
    }
    next.sequence = record->sequence;
    *progress = next;
    tired_error_clear(error);
    return true;
}
