#ifndef TIRED_TRANSACTION_PROGRESS_H
#define TIRED_TRANSACTION_PROGRESS_H
#include "tired/transaction_record.h"
typedef enum
{
    TIRED_PROGRESS_EMPTY,
    TIRED_PROGRESS_FORWARD,
    TIRED_PROGRESS_ROLLBACK,
    TIRED_PROGRESS_COMMITTED,
    TIRED_PROGRESS_ROLLED_BACK
} TiredTransactionProgressMode;
typedef struct
{
    uint64_t sequence;
    TiredTransactionProgressMode mode;
    bool pending, uncertain, had_failure;
    TiredTransactionAction pending_action;
} TiredTransactionProgress;
/* Initialize progress to zero and advance in record order. Enforce preparation,
 * intent/outcome pairing, unresolved-action preservation and terminal boundaries.
 * Atomic on error. Does not validate manifests, operation-specific action order,
 * approvals, or evidence supporting an asserted outcome. */
bool tired_transaction_progress_advance(TiredTransactionProgress *progress,
                                        const TiredTransactionRecord *record, TiredError *error);
#endif
