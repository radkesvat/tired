#ifndef TIRED_LINGER_OBSERVATION_H
#define TIRED_LINGER_OBSERVATION_H
#include "tired/linger_query.h"
typedef struct
{
    bool attempted;
    uint64_t completed_realtime_usec;
    TiredLingerResult result;
} TiredLingerObservation;
/* CLI driver for the invoking account only. Peer-validated system bus, pinned
 * login identity and one bounded deadline across discovery/query. No elevation,
 * activation, account changes or guessed user runtime path. Replaces output;
 * failure is a done unknown result with a diagnostic. Frontends needing terminal
 * concurrency should drive the underlying asynchronous query directly. */
void tired_linger_observe(unsigned timeout_ms, TiredLingerObservation *observation);
#endif
