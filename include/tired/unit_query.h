#ifndef TIRED_UNIT_QUERY_H
#define TIRED_UNIT_QUERY_H
#include "tired/manager_identity.h"
#include "tired/observation.h"
typedef struct TiredUnitQuery TiredUnitQuery;
typedef enum
{
    TIRED_UNIT_LOADED_ONLY,
    TIRED_UNIT_LOAD_CONFIGURATION
} TiredUnitLookup;
typedef struct
{
    bool done;
    TiredError error;
    /* Historical transport evidence retained on cancellation/error. A valid
     * LoadUnit object reply is not proof that the later property query succeeded. */
    bool configuration_load_queued, configuration_load_acknowledged;
    /* Remaining fields are published only when done with TIRED_OK. */
    bool file_found, object_found;
    const char *unit_name, *file_state, *object_path;
    const TiredUnitObservation *observation;
} TiredUnitQueryResult;
/* Read-only GetUnitFileState, GetUnit and typed Unit/Service GetAll sequence.
 * base is already normalized. Identity must be ready and outlive this query.
 * No LoadUnit, activation, name reservation or mutation occurs. */
bool tired_unit_query_start(TiredManagerIdentity *identity, const TiredText *base,
                            unsigned timeout_ms, TiredUnitQuery **query, TiredError *error);
/* Explicit configuration inspection may use LoadUnit in place of GetUnit. This
 * can load a previously unloaded configuration into manager memory, but never
 * starts/stops a service, enqueues jobs, reloads configuration or enables units.
 * Same pinned identity, no bus activation/interactive authorization, bounded
 * deadline and borrowed identity lifetime. Cancellation cannot undo a load. */
bool tired_unit_query_start_lookup(TiredManagerIdentity *identity, const TiredText *base,
                                   TiredUnitLookup lookup, unsigned timeout_ms,
                                   TiredUnitQuery **query, TiredError *error);
bool tired_unit_query_step(TiredUnitQuery *query);
bool tired_unit_query_poll(TiredUnitQuery *query, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error);
void tired_unit_query_cancel(TiredUnitQuery *query);
TiredUnitQueryResult tired_unit_query_result(const TiredUnitQuery *query);
void tired_unit_query_destroy(TiredUnitQuery *query);
#endif
