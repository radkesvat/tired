#ifndef TIRED_LOAD_PATHS_H
#define TIRED_LOAD_PATHS_H
#include "tired/manager_identity.h"
typedef struct TiredLoadPaths TiredLoadPaths;
typedef struct
{
    bool done;
    TiredError error;
    const TiredTextList *directories; /* Borrowed, only present after successful completion. */
} TiredLoadPathsResult;
/* Decode a v/as UnitPath reply atomically. 1..256 absolute paths, <=4096 bytes
 * each and <=1 MiB total. Preserve order and directory alias spelling. */
bool tired_load_paths_read(sd_bus_message *message, TiredTextList *paths, TiredError *error);
/* Identity must be ready and outlive this query. No environment path overrides. */
bool tired_load_paths_start(TiredManagerIdentity *identity, unsigned timeout_ms,
                            TiredLoadPaths **query, TiredError *error);
bool tired_load_paths_step(TiredLoadPaths *query);
bool tired_load_paths_poll(TiredLoadPaths *query, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error);
void tired_load_paths_cancel(TiredLoadPaths *query);
TiredLoadPathsResult tired_load_paths_result(const TiredLoadPaths *query);
void tired_load_paths_destroy(TiredLoadPaths *query);
#endif
