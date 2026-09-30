#ifndef TIRED_MANAGER_JOB_H
#define TIRED_MANAGER_JOB_H
#include "tired/job_event.h"
#include "tired/manager_identity.h"
typedef enum
{
    TIRED_JOB_START,
    TIRED_JOB_STOP,
    TIRED_JOB_RESTART
} TiredJobAction;
typedef struct TiredManagerJob TiredManagerJob;
typedef struct
{
    bool done, submitted, accepted, finished, rejected;
    uint32_t job_id;
    TiredJobEvent completion;
    TiredError error;
} TiredManagerJobResult;
/* Install JobRemoved match, await Manager.Subscribe, then issue Start/Stop/Restart
 * with mode replace to the verified unique owner. No activation or interactive
 * authorization. Identity outlives operation. Caller authorizes and journals intent
 * first. One deadline covers setup, submission and completion. Up to 32 early
 * selected-unit completions are buffered until the returned job ID is known.
 * Cancel stops waiting; it never cancels or reverses a submitted systemd job. */
bool tired_manager_job_start(TiredManagerIdentity *identity, const char *unit,
                             TiredJobAction action, unsigned timeout_ms,
                             TiredManagerJob **operation, TiredError *error);
bool tired_manager_job_step(TiredManagerJob *operation);
bool tired_manager_job_poll(TiredManagerJob *operation, struct pollfd *descriptor,
                            uint64_t *deadline_usec, TiredError *error);
void tired_manager_job_cancel(TiredManagerJob *operation);
TiredManagerJobResult tired_manager_job_result(const TiredManagerJob *operation);
void tired_manager_job_destroy(TiredManagerJob *operation);
#endif
