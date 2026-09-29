#ifndef TIRED_NAME_QUERY_H
#define TIRED_NAME_QUERY_H
#include "tired/manager_identity.h"
typedef struct TiredNameQuery TiredNameQuery;
typedef struct
{
    bool done;
    TiredError error;
    const TiredText *unit_name; /* Borrowed, only present after successful completion. */
} TiredNameQueryResult;
/* Discover manager UnitPath, include destination, then query successive names.
 * Inputs are copied. Supply the complete pending transaction inventory for this
 * scope; identity must be ready and outlive the query. User-scope destinations
 * must occur in the discovered manager UnitPath. One deadline covers all
 * candidates. Destination is an observation location, not write authorization.
 * No writes, reservations, service loading or activation. */
bool tired_name_query_start(TiredManagerIdentity *identity, const TiredText *base,
                            bool explicit_name, const TiredText *destination,
                            const TiredTextList *pending_names, unsigned timeout_ms,
                            TiredNameQuery **query, TiredError *error);
/* Bounded bus pumping and one candidate inspection per step. Filesystem calls
 * remain synchronous; a stalled filesystem can delay return beyond the deadline.
 * Such a late result is rejected. Success is tentative until locked rechecking. */
bool tired_name_query_step(TiredNameQuery *query);
bool tired_name_query_poll(TiredNameQuery *query, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error);
void tired_name_query_cancel(TiredNameQuery *query);
TiredNameQueryResult tired_name_query_result(const TiredNameQuery *query);
void tired_name_query_destroy(TiredNameQuery *query);
#endif
