#ifndef TIRED_MANAGER_H
#define TIRED_MANAGER_H
#include "tired/value.h"
#include <poll.h>
#include <systemd/sd-bus.h>

typedef struct TiredManagerProbe TiredManagerProbe;
/* Production discovery ignores DBUS_* address overrides. User scope requires
 * XDG_RUNTIME_DIR; system scope uses /run/dbus/system_bus_socket. Returns an owned
 * sd_bus connection with authentication/Hello in progress; drive with a probe.
 * *bus starts NULL. No directory or session is created. */
bool tired_manager_bus_open(bool user_scope, sd_bus **bus, TiredError *error);
/* Internal transport seam: directory is absolute/normalized; leaf is one socket
 * component. Enforces filesystem and SO_PEERCRED checks for root/system or the
 * current UID/user. No external CLI accepts an alternate system directory. */
bool tired_manager_bus_connect_directory(const char *directory, const char *leaf, bool user_scope,
                                         sd_bus **bus, TiredError *error);
typedef struct
{
    bool done;
    TiredError error;
    const char *version, *remote_error_name; /* Borrowed until destroy. */
    uint64_t major_version;
} TiredManagerProbeResult;
/* Caller supplies the intended local bus after validating peer identity. D-Bus
 * authentication/Hello may still be in progress. Takes its own reference;
 * no environment discovery, activation, interactive authorization or mutation.
 * Timeout includes connection progress. *probe starts NULL. */
bool tired_manager_probe_start(sd_bus *bus, unsigned timeout_ms, TiredManagerProbe **probe,
                               TiredError *error);
/* Production discovery can pin the query to a verified manager identity. */
bool tired_manager_probe_start_unique(sd_bus *bus, const char *unique_name, unsigned timeout_ms,
                                      TiredManagerProbe **probe, TiredError *error);
bool tired_manager_probe_step(TiredManagerProbe *probe);
/* Poll descriptor/events and absolute CLOCK_MONOTONIC deadline in microseconds. */
bool tired_manager_probe_poll(TiredManagerProbe *probe, struct pollfd *descriptor,
                              uint64_t *deadline_usec, TiredError *error);
void tired_manager_probe_cancel(TiredManagerProbe *probe);
TiredManagerProbeResult tired_manager_probe_result(const TiredManagerProbe *probe);
/* Cancels its callback slot before releasing the borrowed bus reference. */
void tired_manager_probe_destroy(TiredManagerProbe *probe);
#endif
