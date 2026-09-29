#ifndef TIRED_MANAGER_RELOAD_H
#define TIRED_MANAGER_RELOAD_H
#include "tired/manager_identity.h"
typedef struct TiredManagerReload TiredManagerReload;
typedef struct
{
    bool done, submitted, acknowledged;
    TiredError error;
} TiredManagerReloadResult;
/* Native Manager.Reload, addressed to the verified unique owner. No activation or
 * interactive authorization. Identity must outlive the operation; caller verifies
 * supported manager version, authorization and durable intent before starting.
 * Timeout/cancel/disconnect after submission cannot prove the reload did not run.
 * An acknowledgement means method completion, not service health or file approval. */
bool tired_manager_reload_start(TiredManagerIdentity *identity, unsigned timeout_ms,
                                TiredManagerReload **operation, TiredError *error);
bool tired_manager_reload_step(TiredManagerReload *operation);
bool tired_manager_reload_poll(TiredManagerReload *operation, struct pollfd *descriptor,
                               uint64_t *deadline_usec, TiredError *error);
void tired_manager_reload_cancel(TiredManagerReload *operation);
TiredManagerReloadResult tired_manager_reload_result(const TiredManagerReload *operation);
void tired_manager_reload_destroy(TiredManagerReload *operation);
#endif
