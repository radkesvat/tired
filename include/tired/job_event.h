#ifndef TIRED_JOB_EVENT_H
#define TIRED_JOB_EVENT_H
#include "tired/value.h"
#include <systemd/sd-bus.h>
typedef enum
{
    TIRED_JOB_UNKNOWN,
    TIRED_JOB_DONE,
    TIRED_JOB_CANCELED,
    TIRED_JOB_TIMEOUT,
    TIRED_JOB_FAILED,
    TIRED_JOB_DEPENDENCY,
    TIRED_JOB_SKIPPED,
    TIRED_JOB_INVALID,
    TIRED_JOB_ASSERT,
    TIRED_JOB_UNSUPPORTED,
    TIRED_JOB_COLLECTED,
    TIRED_JOB_ONCE
} TiredJobOutcome;
typedef struct
{
    uint32_t id;
    char path[64], unit[209], result[65];
    TiredJobOutcome outcome;
} TiredJobEvent;
/* Canonical numeric /org/freedesktop/systemd1/job/<uint32> path. Atomic output. */
bool tired_job_path_id(const char *path, uint32_t *id, TiredError *error);
/* Strict JobRemoved uoss signal from the supplied verified unique owner, at the
 * manager object path. Intended for a selected safe service unit, not arbitrary
 * dependency unit names. Checks numeric ID/path agreement and bounded result token.
 * Unknown future results are retained as UNKNOWN, never treated as success.
 * Atomic fixed-size output; no borrowed message storage. Only DONE is job success,
 * and even that is not a runtime-health observation. */
bool tired_job_event_read(sd_bus_message *message, const char *owner, TiredJobEvent *event,
                          TiredError *error);
#endif
