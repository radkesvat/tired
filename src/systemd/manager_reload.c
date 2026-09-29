#include "tired/manager_reload.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

struct TiredManagerReload
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *slot;
    uint64_t deadline;
    TiredManagerReloadResult result;
};
static bool now(uint64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *value = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void fail(TiredManagerReload *operation, TiredStatus status, const char *code,
                 const char *message)
{
    operation->result.done = true;
    tired_error_set(&operation->result.error, status, code, message, 0);
}
static bool current(TiredManagerReload *operation)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        fail(operation, TIRED_CONFLICT, "reload-owner", "Manager identity changed during reload.");
        if (owner.error.status != TIRED_OK)
            operation->result.error = owner.error;
        return false;
    }
    uint64_t time;
    if (!now(&time))
        fail(operation, TIRED_INTERNAL, "reload-clock", "Cannot read reload deadline clock.");
    else if (time >= operation->deadline)
        fail(operation, TIRED_RUNTIME_FAILED, "reload-timeout",
             "Manager reload timed out; its outcome may be unknown.");
    return !operation->result.done;
}
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredManagerReload *operation = userdata;
    if (operation->result.done || !current(operation))
        return 1;
    const sd_bus_error *remote = sd_bus_message_get_error(message);
    if (remote != NULL)
    {
        if (sd_bus_error_has_name(remote, SD_BUS_ERROR_ACCESS_DENIED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_AUTH_FAILED))
            fail(operation, TIRED_AUTHORIZATION, "reload-authorization", "Manager denied reload.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_METHOD) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_INTERFACE))
            fail(operation, TIRED_UNSUPPORTED, "reload-unsupported",
                 "Manager reload method is unavailable.");
        else
            fail(operation, TIRED_RUNTIME_FAILED, "reload-error",
                 "Manager reload failed or its outcome is unknown.");
    }
    else if (sd_bus_message_has_signature(message, "") <= 0)
        fail(operation, TIRED_INVALID, "reload-protocol",
             "Manager reload returned an unexpected reply.");
    else
    {
        operation->result.done = operation->result.acknowledged = true;
        tired_error_clear(&operation->result.error);
    }
    return 1;
}
bool tired_manager_reload_start(TiredManagerIdentity *identity, unsigned timeout_ms,
                                TiredManagerReload **output, TiredError *error)
{
    assert(identity != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.error.status != TIRED_OK || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(error, TIRED_INVALID, "reload-input",
                               "Reload requires a ready identity and bounded deadline.", 0);
    TiredManagerReload *operation = calloc(1, sizeof(*operation));
    if (operation == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate manager reload.", errno);
    operation->identity = identity;
    operation->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    sd_bus_message *message = NULL;
    int rc = -EIO;
    if (!now(&operation->deadline))
        goto failed;
    operation->deadline += (uint64_t)timeout_ms * 1000;
    rc = sd_bus_message_new_method_call(operation->bus, &message, owner.unique_name,
                                        "/org/freedesktop/systemd1",
                                        "org.freedesktop.systemd1.Manager", "Reload");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0)
        rc = sd_bus_call_async(operation->bus, &operation->slot, message, reply, operation,
                               (uint64_t)timeout_ms * 1000);
    if (rc < 0)
        goto failed;
    operation->result.submitted = true;
    sd_bus_message_unref(message);
    *output = operation;
    tired_error_clear(error);
    return true;
failed:
    sd_bus_message_unref(message);
    tired_manager_reload_destroy(operation);
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "reload-start",
                           "Cannot submit manager reload.", -rc);
}
bool tired_manager_reload_step(TiredManagerReload *operation)
{
    assert(operation != NULL);
    if (!operation->result.done && current(operation))
    {
        (void)tired_manager_identity_step(operation->identity);
        TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
        if (owner.error.status != TIRED_OK)
        {
            operation->result.done = true;
            operation->result.error = owner.error;
        }
    }
    if (operation->result.done)
        operation->slot = sd_bus_slot_unref(operation->slot);
    return operation->result.done;
}
bool tired_manager_reload_poll(TiredManagerReload *operation, struct pollfd *descriptor,
                               uint64_t *deadline_usec, TiredError *error)
{
    assert(operation != NULL && descriptor != NULL && deadline_usec != NULL);
    if (operation->result.done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    if (!tired_manager_identity_poll(operation->identity, descriptor, deadline_usec, error))
        return false;
    if (*deadline_usec > operation->deadline)
        *deadline_usec = operation->deadline;
    return true;
}
void tired_manager_reload_cancel(TiredManagerReload *operation)
{
    assert(operation != NULL);
    if (!operation->result.done)
    {
        fail(operation, TIRED_CANCELLED, "reload-cancelled",
             "Reload wait cancelled; the submitted operation may still complete.");
        operation->slot = sd_bus_slot_unref(operation->slot);
    }
}
TiredManagerReloadResult tired_manager_reload_result(const TiredManagerReload *operation)
{
    assert(operation != NULL);
    TiredManagerReloadResult result = operation->result;
    TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
    if (result.error.status == TIRED_OK && owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
    }
    return result;
}
void tired_manager_reload_destroy(TiredManagerReload *operation)
{
    if (operation == NULL)
        return;
    sd_bus_slot_unref(operation->slot);
    sd_bus_unref(operation->bus);
    free(operation);
}
