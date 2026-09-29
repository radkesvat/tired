#include "tired/manager.h"
#include "tired/capture.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct TiredManagerProbe
{
    sd_bus *bus;
    sd_bus_slot *slot;
    TiredText version, remote_error;
    uint64_t major, deadline;
    bool done;
    TiredError error;
};
static bool now_usec(uint64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *value = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void failed(TiredManagerProbe *probe, TiredStatus status, const char *code,
                   const char *message, int system_errno)
{
    probe->done = true;
    tired_error_set(&probe->error, status, code, message, system_errno);
}
static bool named(const char *name, const char *expected) { return strcmp(name, expected) == 0; }
static void transport_failure(TiredManagerProbe *probe, int rc)
{
    if (rc == -EACCES || rc == -EPERM)
        failed(probe, TIRED_AUTHORIZATION, "manager-authorization",
               "Manager bus access was denied.", -rc);
    else if (rc == -EBADMSG || rc == -EPROTO)
        failed(probe, TIRED_INVALID, "manager-protocol",
               "Manager bus returned malformed protocol data.", -rc);
    else if (rc == -ENOMEM)
        failed(probe, TIRED_INTERNAL, "allocation", "Cannot allocate manager bus state.", -rc);
    else if (rc == -ENOTCONN || rc == -ECONNRESET || rc == -EPIPE)
        failed(probe, TIRED_RUNTIME_FAILED, "manager-disconnected", "Manager bus disconnected.",
               -rc);
    else
        failed(probe, TIRED_RUNTIME_FAILED, "manager-io", "Cannot process manager bus traffic.",
               -rc);
}
static int version_reply(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    TiredManagerProbe *probe = userdata;
    probe->done = true;
    const sd_bus_error *remote = sd_bus_message_get_error(message);
    if (remote != NULL)
    {
        const char *name = remote->name == NULL ? "unknown" : remote->name;
        size_t length = strnlen(name, 257);
        if (length > 256)
        {
            failed(probe, TIRED_INVALID, "manager-protocol",
                   "Manager error name exceeds its bound.", 0);
            return 1;
        }
        if (!tired_validate_text(name, length, true, &probe->error) ||
            !tired_text_set(&probe->remote_error, name, length, 256, &probe->error))
            return 1;
        if (named(name, SD_BUS_ERROR_ACCESS_DENIED) || named(name, SD_BUS_ERROR_AUTH_FAILED) ||
            named(name, SD_BUS_ERROR_INTERACTIVE_AUTHORIZATION_REQUIRED))
            failed(probe, TIRED_AUTHORIZATION, "manager-authorization",
                   "Manager access was denied.", 0);
        else if (named(name, SD_BUS_ERROR_SERVICE_UNKNOWN) ||
                 named(name, SD_BUS_ERROR_NAME_HAS_NO_OWNER))
            failed(probe, TIRED_NOT_FOUND, "manager-unavailable",
                   "No service manager owns the requested bus name.", 0);
        else if (named(name, SD_BUS_ERROR_DISCONNECTED) ||
                 (named(name, SD_BUS_ERROR_NO_REPLY) && sd_bus_is_open(probe->bus) <= 0))
            failed(probe, TIRED_RUNTIME_FAILED, "manager-disconnected", "Manager bus disconnected.",
                   0);
        else if (named(name, SD_BUS_ERROR_NO_REPLY) || named(name, SD_BUS_ERROR_TIMEOUT))
            failed(probe, TIRED_RUNTIME_FAILED, "manager-timeout", "Manager discovery timed out.",
                   0);
        else if (named(name, SD_BUS_ERROR_UNKNOWN_PROPERTY) ||
                 named(name, SD_BUS_ERROR_UNKNOWN_METHOD) ||
                 named(name, SD_BUS_ERROR_UNKNOWN_INTERFACE))
            failed(probe, TIRED_UNSUPPORTED, "manager-property",
                   "Manager version property is unavailable.", 0);
        else
            failed(probe, TIRED_RUNTIME_FAILED, "manager-error",
                   "Manager returned an error during discovery.", 0);
        return 1;
    }
    const char *version = NULL;
    if (sd_bus_message_has_signature(message, "v") <= 0 ||
        sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "s") <= 0 ||
        sd_bus_message_read(message, "s", &version) <= 0 ||
        sd_bus_message_exit_container(message) < 0 || sd_bus_message_at_end(message, true) <= 0)
    {
        failed(probe, TIRED_INVALID, "manager-protocol",
               "Manager returned an invalid version property type.", 0);
        return 1;
    }
    size_t length = strnlen(version, 257), digits = 0;
    if (length > 256)
    {
        failed(probe, TIRED_INVALID, "manager-protocol", "Manager version exceeds its bound.", 0);
        return 1;
    }
    if (!tired_validate_text(version, length, true, &probe->error) ||
        !tired_text_set(&probe->version, version, length, 256, &probe->error))
        return 1;
    while (digits < length && version[digits] >= '0' && version[digits] <= '9')
        ++digits;
    if (digits == 0 || (digits < length && strchr(".-+~ ", version[digits]) == NULL) ||
        !tired_parse_u64(version, digits, 1, 9999, &probe->major, &probe->error))
    {
        failed(probe, TIRED_UNSUPPORTED, "manager-version-format",
               "Manager version is not a recognized numbered release.", 0);
        return 1;
    }
    if (probe->major < 249)
        failed(probe, TIRED_UNSUPPORTED, "manager-version",
               "This manager is older than the supported systemd 249 baseline.", 0);
    return 1;
}
bool tired_manager_probe_start(sd_bus *bus, unsigned timeout_ms, TiredManagerProbe **output,
                               TiredError *error)
{
    assert(bus != NULL && output != NULL && *output == NULL);
    if (timeout_ms == 0 || timeout_ms > 300000 || sd_bus_is_open(bus) <= 0)
        return tired_error_set(error, TIRED_INVALID, "manager-probe-input",
                               "Expected an open bus and a bounded discovery deadline.", 0);
    TiredManagerProbe *probe = calloc(1, sizeof(*probe));
    if (probe == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate manager discovery.", errno);
    probe->bus = sd_bus_ref(bus);
    sd_bus_message *request = NULL;
    int rc = -EIO;
    if (!now_usec(&probe->deadline))
    {
        rc = -errno;
        goto fail;
    }
    probe->deadline += (uint64_t)timeout_ms * 1000;
    rc = sd_bus_message_new_method_call(bus, &request, "org.freedesktop.systemd1",
                                        "/org/freedesktop/systemd1",
                                        "org.freedesktop.DBus.Properties", "Get");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(request, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(request, 0);
    if (rc >= 0)
        rc = sd_bus_message_append(request, "ss", "org.freedesktop.systemd1.Manager", "Version");
    if (rc >= 0)
        rc = sd_bus_call_async(bus, &probe->slot, request, version_reply, probe,
                               (uint64_t)timeout_ms * 1000);
    if (rc < 0)
        goto fail;
    sd_bus_message_unref(request);
    *output = probe;
    tired_error_clear(error);
    return true;
fail:
    sd_bus_message_unref(request);
    transport_failure(probe, rc);
    if (error != NULL)
        *error = probe->error;
    tired_manager_probe_destroy(probe);
    return false;
}
bool tired_manager_probe_step(TiredManagerProbe *probe)
{
    assert(probe != NULL);
    if (probe->done)
        return true;
    uint64_t now;
    if (!now_usec(&now))
        failed(probe, TIRED_INTERNAL, "manager-clock", "Cannot read discovery clock.", errno);
    else if (now >= probe->deadline)
        failed(probe, TIRED_RUNTIME_FAILED, "manager-timeout", "Manager discovery timed out.", 0);
    for (unsigned i = 0; !probe->done && i < 16; ++i)
    {
        int rc = sd_bus_process(probe->bus, NULL);
        if (rc < 0)
        {
            transport_failure(probe, rc);
            break;
        }
        if (rc == 0)
            break;
    }
    if (probe->done)
        probe->slot = sd_bus_slot_unref(probe->slot);
    return probe->done;
}
bool tired_manager_probe_poll(TiredManagerProbe *probe, struct pollfd *descriptor,
                              uint64_t *deadline_usec, TiredError *error)
{
    assert(probe != NULL && descriptor != NULL && deadline_usec != NULL);
    if (probe->done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    int fd = sd_bus_get_fd(probe->bus), events = sd_bus_get_events(probe->bus);
    uint64_t deadline;
    int rc = sd_bus_get_timeout(probe->bus, &deadline);
    if (fd < 0 || events < 0 || rc < 0)
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "manager-poll",
                               "Cannot obtain manager polling state.",
                               fd < 0       ? -fd
                               : events < 0 ? -events
                                            : -rc);
    *descriptor = (struct pollfd){.fd = fd, .events = (short)events};
    *deadline_usec = deadline < probe->deadline ? deadline : probe->deadline;
    tired_error_clear(error);
    return true;
}
void tired_manager_probe_cancel(TiredManagerProbe *probe)
{
    assert(probe != NULL);
    if (!probe->done)
    {
        failed(probe, TIRED_CANCELLED, "manager-cancelled", "Manager discovery was cancelled.", 0);
        probe->slot = sd_bus_slot_unref(probe->slot);
    }
}
TiredManagerProbeResult tired_manager_probe_result(const TiredManagerProbe *probe)
{
    assert(probe != NULL);
    return (TiredManagerProbeResult){.done = probe->done,
                                     .error = probe->error,
                                     .version = probe->version.data,
                                     .remote_error_name = probe->remote_error.data,
                                     .major_version = probe->major};
}
void tired_manager_probe_destroy(TiredManagerProbe *probe)
{
    if (probe == NULL)
        return;
    sd_bus_slot_unref(probe->slot);
    sd_bus_unref(probe->bus);
    tired_text_destroy(&probe->version);
    tired_text_destroy(&probe->remote_error);
    free(probe);
}
