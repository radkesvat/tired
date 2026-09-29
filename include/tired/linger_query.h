#ifndef TIRED_LINGER_QUERY_H
#define TIRED_LINGER_QUERY_H
#include "tired/manager_identity.h"
typedef struct TiredLingerQuery TiredLingerQuery;
typedef struct
{
    bool done, known, enabled;
    uid_t uid;
    TiredError error;
} TiredLingerResult;
/* Read-only GetUser(uid), User.UID verification, then User.Linger. Requires a
 * ready LOGIN identity which outlives the query. Caller authorizes account UID.
 * One monotonic deadline covers all stages. No activation, interactive auth,
 * guessed user object path, session creation, or SetUserLinger call. Missing user
 * objects/properties are unavailable, never known false. */
bool tired_linger_query_start(TiredManagerIdentity *identity, uid_t uid, unsigned timeout_ms,
                              TiredLingerQuery **query, TiredError *error);
bool tired_linger_query_step(TiredLingerQuery *query);
bool tired_linger_query_poll(TiredLingerQuery *query, struct pollfd *descriptor,
                             uint64_t *deadline_usec, TiredError *error);
void tired_linger_query_cancel(TiredLingerQuery *query);
TiredLingerResult tired_linger_query_result(const TiredLingerQuery *query);
void tired_linger_query_destroy(TiredLingerQuery *query);
#endif
