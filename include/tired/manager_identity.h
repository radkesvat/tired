#ifndef TIRED_MANAGER_IDENTITY_H
#define TIRED_MANAGER_IDENTITY_H
#include "tired/value.h"
#include <poll.h>
#include <systemd/sd-bus.h>

typedef struct TiredManagerIdentity TiredManagerIdentity;
typedef struct
{
    bool ready, changed;
    const char *unique_name; /* Borrowed until destroy, retained on invalidation. */
    uid_t uid;
    TiredError error;
} TiredManagerIdentityResult;
/* Install NameOwnerChanged match before resolving owner and UID. System expects
 * UID 0; user scope expects the current UID. Uses an already peer-validated broker
 * connection. No service activation or interactive authorization. */
bool tired_manager_identity_start(sd_bus *bus, bool user_scope, unsigned timeout_ms,
                                  TiredManagerIdentity **identity, TiredError *error);
/* Nonblocking pump. Returns true when initial discovery completes or fails.
 * Keep pumping the bus after ready to receive invalidation. Future method calls
 * must target the captured unique name and check ready/error before admission. */
bool tired_manager_identity_step(TiredManagerIdentity *identity);
bool tired_manager_identity_poll(TiredManagerIdentity *identity, struct pollfd *descriptor,
                                 uint64_t *deadline_usec, TiredError *error);
TiredManagerIdentityResult tired_manager_identity_result(const TiredManagerIdentity *identity);
void tired_manager_identity_destroy(TiredManagerIdentity *identity);
#endif
