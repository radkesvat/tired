#ifndef TIRED_MANAGER_ENABLEMENT_H
#define TIRED_MANAGER_ENABLEMENT_H
#include "tired/manager_identity.h"
#include "tired/unit_file_changes.h"
typedef struct TiredManagerEnablement TiredManagerEnablement;
typedef struct
{
    bool done, submitted, acknowledged;
    TiredError error;
    const TiredUnitFileChanges *changes;
} TiredManagerEnablementResult;
/* Persistent enable/disable of exactly one safe service name; runtime=false and
 * force=false. Verified unique owner, no activation/interactive authorization.
 * Caller authorizes and journals intent, inspects applicable install directives
 * (including Also= effects), and verifies final state. Identity outlives operation.
 * Cancellation/timeouts do not undo a submitted mutation. */
bool tired_manager_enablement_start(TiredManagerIdentity *identity, const char *unit, bool enable,
                                    unsigned timeout_ms, TiredManagerEnablement **operation,
                                    TiredError *error);
bool tired_manager_enablement_step(TiredManagerEnablement *operation);
bool tired_manager_enablement_poll(TiredManagerEnablement *operation, struct pollfd *descriptor,
                                   uint64_t *deadline_usec, TiredError *error);
void tired_manager_enablement_cancel(TiredManagerEnablement *operation);
TiredManagerEnablementResult
tired_manager_enablement_result(const TiredManagerEnablement *operation);
void tired_manager_enablement_destroy(TiredManagerEnablement *operation);
#endif
