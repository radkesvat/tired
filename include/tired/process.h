#ifndef TIRED_PROCESS_H
#define TIRED_PROCESS_H
#include "tired/value.h"

typedef struct TiredProcess TiredProcess;
typedef enum
{
    TIRED_PROCESS_RUNNING,
    TIRED_PROCESS_EXITED,
    TIRED_PROCESS_SIGNALED,
    TIRED_PROCESS_TIMEOUT,
    TIRED_PROCESS_CANCELLED,
    TIRED_PROCESS_OUTPUT_LIMIT,
    TIRED_PROCESS_IO_ERROR
} TiredProcessOutcome;
typedef struct
{
    TiredProcessOutcome outcome;
    int exit_code, signal_number, system_errno;
    const char *standard_output, *standard_error;
    size_t output_length, error_length;
} TiredProcessResult;
/* Explicit absolute executable, literal NULL-terminated argv and environment.
 * No PATH search or inherited environment. Caller establishes executable trust.
 * Limits: 4096 entries/1 MiB combined input; 1..300000 ms; <=1 MiB per output.
 * Output pointer must initially be NULL. No workload discovery may call this API. */
bool tired_process_start(const char *executable, char *const argv[], char *const environment[],
                         unsigned timeout_ms, size_t output_limit, TiredProcess **process,
                         TiredError *error);
/* Nonblocking pump: drain bounded chunks from both pipes, enforce the monotonic
 * deadline, and reap the child. Call regularly (normally every 10–25 ms).
 * true means terminal (normally reaped; IO_ERROR includes external reaping).
 * No other component may reap this child or change SIGCHLD disposition while
 * active. The process group is for trusted helpers, not an isolation sandbox. */
bool tired_process_step(TiredProcess *process);
/* Sends SIGKILL to the isolated process group and closes capture pipes. Continue
 * stepping until terminal before destroy, including after cancellation. */
void tired_process_cancel(TiredProcess *process);
/* Borrowed buffers; invalidated by the next step or destroy. May contain binary
 * data or terminal controls; lengths are authoritative. Escape before displaying. */
TiredProcessResult tired_process_result(const TiredProcess *process);
/* Valid only after step returned true. NULL is allowed. */
void tired_process_destroy(TiredProcess *process);
#endif
