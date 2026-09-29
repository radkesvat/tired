#ifndef TIRED_STATUS_FRONTEND_H
#define TIRED_STATUS_FRONTEND_H
#include "tired/cli.h"
#include "tired/linger_observation.h"
#include "tired/service_files.h"
#include "tired/unit_batch.h"
/* Borrowed inputs, valid for the complete rendering call. A NULL record requires
 * record_error explaining absence or failed inspection. Files apply only when
 * record is present. Live values require a completed successful query. */
typedef struct
{
    const char *unit_name;
    bool user_scope;
    const TiredServiceRecord *record;
    TiredError record_error, transactions_error;
    bool transaction_pending;
    TiredServiceFiles files;
    TiredUnitBatchItem live;
    TiredLingerObservation linger;
} TiredStatusView;
/* Pure display and exit semantics, no filesystem or manager calls. Outputs are
 * atomic. Contains no argv, environment values, credentials or private model. */
bool tired_status_output(const TiredStatusView *view, bool json, bool check_active,
                         TiredText *output, TiredStatus *result, TiredError *error);
/* Read-only production discovery and bounded fresh manager query. */
bool tired_status_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                          TiredError *error);
#endif
