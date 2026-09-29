#ifndef TIRED_RECOVER_LIVE_H
#define TIRED_RECOVER_LIVE_H
#include "tired/manager.h"
#include "tired/unit_batch.h"
#include <json-c/json.h>
typedef struct
{
    sd_bus *bus;
    TiredManagerIdentity *identity;
    TiredManagerProbe *version;
    TiredUnitBatch *batch;
    TiredError error;
} TiredRecoveryLive;
/* CLI-only synchronous driver with a five-second aggregate monotonic budget.
 * Owns all transport/query handles until destroy; failures remain in error. */
void tired_recover_live_collect(bool user, const TiredTextList *names, TiredRecoveryLive *live);
TiredUnitBatchItem tired_recover_live_item(const TiredRecoveryLive *live, size_t index);
struct json_object *tired_recover_live_json(const TiredUnitBatchItem *item);
void tired_recover_live_destroy(TiredRecoveryLive *live);
#endif
