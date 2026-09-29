#include "tired/transaction_progress.h"
#include <stdio.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static bool advance(TiredTransactionProgress *progress, TiredTransactionAction action,
                    TiredTransactionActionState state, TiredError *error)
{
    TiredTransactionRecord record = {
        .sequence = progress->sequence + 1, .action = action, .state = state};
    return tired_transaction_progress_advance(progress, &record, error);
}
int main(void)
{
    TiredTransactionProgress progress = {0};
    TiredError error = {0};
    CHECK(!advance(&progress, TIRED_ACTION_START, TIRED_ACTION_INTENT, &error));
    CHECK(progress.sequence == 0 && progress.mode == TIRED_PROGRESS_EMPTY);
    CHECK(advance(&progress, TIRED_ACTION_PREPARE, TIRED_ACTION_COMPLETED, &error));
    CHECK(!advance(&progress, TIRED_ACTION_PREPARE, TIRED_ACTION_COMPLETED, &error));
    CHECK(!advance(&progress, TIRED_ACTION_PUBLISH_FILES, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_PUBLISH_FILES, TIRED_ACTION_INTENT, &error));
    CHECK(!advance(&progress, TIRED_ACTION_RELOAD, TIRED_ACTION_COMPLETED, &error));
    CHECK(progress.sequence == 2 && progress.pending_action == TIRED_ACTION_PUBLISH_FILES);
    CHECK(advance(&progress, TIRED_ACTION_PUBLISH_FILES, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_START, TIRED_ACTION_INTENT, &error));
    CHECK(progress.pending && !progress.uncertain);
    CHECK(advance(&progress, TIRED_ACTION_START, TIRED_ACTION_UNCERTAIN, &error));
    CHECK(progress.pending && progress.uncertain);
    CHECK(!advance(&progress, TIRED_ACTION_START, TIRED_ACTION_INTENT, &error));
    CHECK(!advance(&progress, TIRED_ACTION_COMMIT, TIRED_ACTION_INTENT, &error));
    CHECK(!advance(&progress, TIRED_ACTION_ROLLBACK, TIRED_ACTION_INTENT, &error));
    CHECK(progress.sequence == 5 && progress.pending && progress.uncertain);
    CHECK(advance(&progress, TIRED_ACTION_START, TIRED_ACTION_FAILED, &error));
    CHECK(!progress.pending && !progress.uncertain && progress.had_failure);
    /* Known runtime failure can retain the installed service; observation and
     * final evidence still belong to the operation-specific controller. */
    CHECK(advance(&progress, TIRED_ACTION_OBSERVE, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_OBSERVE, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_STORE_RECORD, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_STORE_RECORD, TIRED_ACTION_COMPLETED, &error));
    CHECK(!advance(&progress, TIRED_ACTION_COMMIT, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_COMMIT, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_COMMIT, TIRED_ACTION_COMPLETED, &error));
    CHECK(progress.mode == TIRED_PROGRESS_COMMITTED && progress.had_failure && !progress.pending);
    CHECK(!advance(&progress, TIRED_ACTION_START, TIRED_ACTION_INTENT, &error));
    progress = (TiredTransactionProgress){0};
    CHECK(advance(&progress, TIRED_ACTION_PREPARE, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_PUBLISH_FILES, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_PUBLISH_FILES, TIRED_ACTION_FAILED, &error));
    CHECK(advance(&progress, TIRED_ACTION_ROLLBACK, TIRED_ACTION_INTENT, &error));
    CHECK(progress.mode == TIRED_PROGRESS_ROLLBACK);
    CHECK(!advance(&progress, TIRED_ACTION_COMMIT, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_STOP, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_STOP, TIRED_ACTION_UNCERTAIN, &error));
    CHECK(!advance(&progress, TIRED_ACTION_ROLLBACK, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_STOP, TIRED_ACTION_FAILED, &error));
    CHECK(advance(&progress, TIRED_ACTION_STOP, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_STOP, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_REMOVE_FILES, TIRED_ACTION_INTENT, &error));
    CHECK(advance(&progress, TIRED_ACTION_REMOVE_FILES, TIRED_ACTION_COMPLETED, &error));
    CHECK(advance(&progress, TIRED_ACTION_ROLLBACK, TIRED_ACTION_COMPLETED, &error));
    CHECK(progress.mode == TIRED_PROGRESS_ROLLED_BACK && !progress.pending);
    CHECK(!advance(&progress, TIRED_ACTION_ROLLBACK, TIRED_ACTION_INTENT, &error));
    progress = (TiredTransactionProgress){0};
    TiredTransactionRecord bad = {
        .sequence = 2, .action = TIRED_ACTION_PREPARE, .state = TIRED_ACTION_COMPLETED};
    CHECK(!tired_transaction_progress_advance(&progress, &bad, &error));
    CHECK(!advance(&progress, (TiredTransactionAction)-1, TIRED_ACTION_INTENT, &error));
    CHECK(!advance(&progress, TIRED_ACTION_PREPARE, (TiredTransactionActionState)-1, &error));
    CHECK(progress.sequence == 0);
    return 0;
}
