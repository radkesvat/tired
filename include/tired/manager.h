#ifndef TIRED_MANAGER_H
#define TIRED_MANAGER_H
#include "tired/value.h"
#include <poll.h>
#include <systemd/sd-bus.h>

typedef struct TiredManagerProbe TiredManagerProbe;
typedef struct
{
    bool done;
    TiredError error;
    const char *version, *remote_error_name; /* Borrowed until destroy. */
    uint64_t major_version;
} TiredManagerProbeResult;
/* Caller supplies the intended authenticated local bus. Takes its own reference;
 * no environment discovery, activation, interactive authorization or mutation.
 * Timeout includes connection progress. *probe starts NULL. */
bool tired_manager_probe_start(sd_bus *bus, unsigned timeout_ms, TiredManagerProbe **probe,
                               TiredError *error);
bool tired_manager_probe_step(TiredManagerProbe *probe);
/* Poll descriptor/events and absolute CLOCK_MONOTONIC deadline in microseconds. */
bool tired_manager_probe_poll(TiredManagerProbe *probe, struct pollfd *descriptor,
                              uint64_t *deadline_usec, TiredError *error);
void tired_manager_probe_cancel(TiredManagerProbe *probe);
TiredManagerProbeResult tired_manager_probe_result(const TiredManagerProbe *probe);
/* Cancels its callback slot before releasing the borrowed bus reference. */
void tired_manager_probe_destroy(TiredManagerProbe *probe);
#endif
