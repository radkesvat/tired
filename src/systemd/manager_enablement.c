#include "tired/manager_enablement.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct TiredManagerEnablement
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *slot;
    uint64_t deadline;
    bool enable;
    TiredUnitFileChanges changes;
    TiredManagerEnablementResult result;
};
static bool now(uint64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *value = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void fail(TiredManagerEnablement *operation, TiredStatus status, const char *code,
                 const char *message)
{
    operation->result.done = true;
    tired_error_set(&operation->result.error, status, code, message, 0);
}
static bool current(TiredManagerEnablement *operation)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        fail(operation, TIRED_CONFLICT, "enablement-owner",
             "Manager identity changed during enablement.");
        if (owner.error.status != TIRED_OK)
            operation->result.error = owner.error;
        return false;
    }
    uint64_t time;
    if (!now(&time))
        fail(operation, TIRED_INTERNAL, "enablement-clock", "Cannot read enablement clock.");
    else if (time >= operation->deadline)
        fail(operation, TIRED_RUNTIME_FAILED, "enablement-timeout",
             "Enablement timed out; submitted changes may have occurred.");
    return !operation->result.done;
}
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredManagerEnablement *operation = userdata;
    if (operation->result.done || !current(operation))
        return 1;
    const sd_bus_error *remote = sd_bus_message_get_error(message);
    if (remote != NULL)
    {
        if (sd_bus_error_has_name(remote, SD_BUS_ERROR_ACCESS_DENIED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_AUTH_FAILED))
            fail(operation, TIRED_AUTHORIZATION, "enablement-authorization",
                 "Manager denied unit-file changes.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_METHOD) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_INTERFACE))
            fail(operation, TIRED_UNSUPPORTED, "enablement-unsupported",
                 "Manager unit-file method is unavailable.");
        else
            fail(operation, TIRED_RUNTIME_FAILED, "enablement-error",
                 "Unit-file operation failed or its outcome is unknown.");
    }
    else
    {
        operation->result.done = true;
        operation->result.acknowledged = tired_unit_file_changes_read(
            message, operation->enable, &operation->changes, &operation->result.error);
    }
    return 1;
}
bool tired_manager_enablement_start(TiredManagerIdentity *identity, const char *unit, bool enable,
                                    unsigned timeout_ms, TiredManagerEnablement **output,
                                    TiredError *error)
{
    assert(identity != NULL && unit != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    size_t length = strnlen(unit, 209);
    if (!owner.ready || owner.error.status != TIRED_OK || timeout_ms == 0 || timeout_ms > 300000 ||
        length <= 8 || length > 208 || memcmp(unit + length - 8, ".service", 8) != 0 ||
        !tired_name_validate_base(unit, length - 8, error))
        return tired_error_set(
            error, TIRED_INVALID, "enablement-input",
            "Enablement requires a ready identity, safe service and bounded deadline.", 0);
    TiredManagerEnablement *operation = calloc(1, sizeof(*operation));
    if (operation == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate enablement operation.", errno);
    operation->identity = identity;
    operation->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    operation->enable = enable;
    sd_bus_message *message = NULL;
    int rc = -EIO;
    if (!now(&operation->deadline))
        goto failed;
    operation->deadline += (uint64_t)timeout_ms * 1000;
    rc = sd_bus_message_new_method_call(
        operation->bus, &message, owner.unique_name, "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", enable ? "EnableUnitFiles" : "DisableUnitFiles");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "s");
    if (rc >= 0)
        rc = sd_bus_message_append(message, "s", unit);
    if (rc >= 0)
        rc = sd_bus_message_close_container(message);
    if (rc >= 0)
        rc = sd_bus_message_append(message, "b", 0);
    if (rc >= 0 && enable)
        rc = sd_bus_message_append(message, "b", 0);
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
    tired_manager_enablement_destroy(operation);
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "enablement-start",
                           "Cannot submit unit-file operation.", -rc);
}
bool tired_manager_enablement_step(TiredManagerEnablement *operation)
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
bool tired_manager_enablement_poll(TiredManagerEnablement *operation, struct pollfd *descriptor,
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
void tired_manager_enablement_cancel(TiredManagerEnablement *operation)
{
    assert(operation != NULL);
    if (!operation->result.done)
        fail(operation, TIRED_CANCELLED, "enablement-cancelled",
             "Enablement wait cancelled; submitted changes may still complete.");
    operation->slot = sd_bus_slot_unref(operation->slot);
}
TiredManagerEnablementResult
tired_manager_enablement_result(const TiredManagerEnablement *operation)
{
    assert(operation != NULL);
    TiredManagerEnablementResult result = operation->result;
    TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
    if (result.error.status == TIRED_OK && owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
    }
    if (result.acknowledged && result.error.status == TIRED_OK)
        result.changes = &operation->changes;
    return result;
}
void tired_manager_enablement_destroy(TiredManagerEnablement *operation)
{
    if (operation == NULL)
        return;
    sd_bus_slot_unref(operation->slot);
    sd_bus_unref(operation->bus);
    tired_unit_file_changes_destroy(&operation->changes);
    free(operation);
}
