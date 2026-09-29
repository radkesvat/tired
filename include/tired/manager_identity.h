#ifndef TIRED_MANAGER_IDENTITY_H
#define TIRED_MANAGER_IDENTITY_H
#include "tired/value.h"
#include <poll.h>
#include <systemd/sd-bus.h>

typedef struct TiredManagerIdentity TiredManagerIdentity;
typedef enum
{
    TIRED_MANAGER_SYSTEMD,
    TIRED_MANAGER_LOGIN
} TiredManagerKind;
typedef struct
{
    bool ready, changed, user_scope;
    TiredManagerKind kind;
    const char *unique_name; /* Borrowed until destroy, retained on invalidation. */
    uid_t uid;
    TiredError error;
} TiredManagerIdentityResult;
/* Install NameOwnerChanged match before resolving owner and UID. System expects
 * UID 0; user scope expects the current UID. Uses an already peer-validated broker
 * connection. No service activation or interactive authorization. */
bool tired_manager_identity_start(sd_bus *bus, bool user_scope, unsigned timeout_ms,
                                  TiredManagerIdentity **identity, TiredError *error);
/* Pin org.freedesktop.login1 on a peer-validated system broker connection. Always
 * requires UID 0, independently of the user whose account will be inspected.
 * Never activates logind. The returned LOGIN identity is not a service-manager
 * identity and must not be used for unit operations. */
bool tired_login_identity_start(sd_bus *bus, unsigned timeout_ms, TiredManagerIdentity **identity,
                                TiredError *error);
/* Nonblocking pump. Returns true when initial discovery completes or fails.
 * Keep pumping the bus after ready to receive invalidation. Future method calls
 * must target the captured unique name and check ready/error before admission. */
bool tired_manager_identity_step(TiredManagerIdentity *identity);
bool tired_manager_identity_poll(TiredManagerIdentity *identity, struct pollfd *descriptor,
                                 uint64_t *deadline_usec, TiredError *error);
TiredManagerIdentityResult tired_manager_identity_result(const TiredManagerIdentity *identity);
/* Borrowed connection for backend operations tied to this identity. */
sd_bus *tired_manager_identity_bus(TiredManagerIdentity *identity);
void tired_manager_identity_destroy(TiredManagerIdentity *identity);
#endif
