#ifndef TIRED_INSPECTION_LIVE_H
#define TIRED_INSPECTION_LIVE_H
#include "tired/manager.h"
#include "tired/unit_batch.h"
#include <json-c/json.h>
typedef struct
{
    sd_bus *bus;
    TiredManagerIdentity *identity;
    TiredManagerProbe *version;
    TiredUnitBatch *batch;
    TiredUnitQuery *configuration;
    uint64_t configuration_completed_usec;
    TiredError error;
} TiredInspectionLive;
/* CLI-only synchronous driver with a five-second aggregate monotonic budget.
 * Owns all transport/query handles until destroy; failures remain in error. */
void tired_inspection_live_collect(bool user, const TiredTextList *names,
                                   TiredInspectionLive *live);
/* Explicit LoadUnit configuration lookup for one full unit name, with the same
 * aggregate deadline. May create an in-memory unit object, never a service job. */
void tired_inspection_configuration_collect(bool user, const TiredText *name,
                                            TiredInspectionLive *live);
TiredUnitBatchItem tired_inspection_live_item(const TiredInspectionLive *live, size_t index);
struct json_object *tired_inspection_live_json(const TiredUnitBatchItem *item);
void tired_inspection_live_destroy(TiredInspectionLive *live);
#endif
