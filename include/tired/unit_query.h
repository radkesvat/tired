#ifndef TIRED_UNIT_QUERY_H
#define TIRED_UNIT_QUERY_H
#include "tired/manager_identity.h"
#include "tired/observation.h"
typedef struct TiredUnitQuery TiredUnitQuery;
typedef struct
{
    bool done;
    TiredError error;
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
bool tired_unit_query_step(TiredUnitQuery *query);
bool tired_unit_query_poll(TiredUnitQuery *query, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error);
void tired_unit_query_cancel(TiredUnitQuery *query);
TiredUnitQueryResult tired_unit_query_result(const TiredUnitQuery *query);
void tired_unit_query_destroy(TiredUnitQuery *query);
#endif
