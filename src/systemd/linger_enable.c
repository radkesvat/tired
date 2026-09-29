#include "tired/linger_enable.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
struct TiredLingerEnable
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *slot;
    TiredLingerQuery *query;
    uint64_t deadline;
    TiredLingerEnableResult result;
};
static bool now(uint64_t *value)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *value = (uint64_t)time.tv_sec * 1000000U + (uint64_t)time.tv_nsec / 1000U;
    return true;
}
static void fail(TiredLingerEnable *operation, TiredStatus status, const char *code,
                 const char *message)
{
    operation->result.done = true;
    tired_error_set(&operation->result.error, status, code, message, 0);
}
static bool current(TiredLingerEnable *operation, unsigned *left)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
    uint64_t time;
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        fail(operation, TIRED_CONFLICT, "linger-enable-owner",
             "Login manager identity changed during lingering enablement.");
        if (owner.error.status != TIRED_OK)
            operation->result.error = owner.error;
    }
    else if (!now(&time))
        fail(operation, TIRED_INTERNAL, "linger-enable-clock",
             "Cannot read lingering enablement clock.");
    else if (time >= operation->deadline)
        fail(operation, TIRED_RUNTIME_FAILED, "linger-enable-timeout",
             "Lingering enablement timed out; a submitted change may still complete.");
    else
        *left = (unsigned)((operation->deadline - time + 999U) / 1000U);
    return !operation->result.done;
}
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredLingerEnable *operation = userdata;
    unsigned left;
    if (operation->result.done || !current(operation, &left))
        return 1;
    const sd_bus_error *remote = sd_bus_message_get_error(message);
    if (remote != NULL)
    {
        if (sd_bus_error_has_name(remote, SD_BUS_ERROR_ACCESS_DENIED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_AUTH_FAILED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_INTERACTIVE_AUTHORIZATION_REQUIRED))
            fail(operation, TIRED_AUTHORIZATION, "linger-enable-authorization",
                 "Login manager denied lingering enablement.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_METHOD) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_INTERFACE))
            fail(operation, TIRED_UNSUPPORTED, "linger-enable-unsupported",
                 "Login manager lingering enablement is unavailable.");
        else if (sd_bus_error_has_name(remote, "org.freedesktop.login1.NoSuchUser"))
            fail(operation, TIRED_NOT_FOUND, "linger-enable-user",
                 "Login manager could not find the requested account.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_NO_REPLY) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_TIMEOUT))
            fail(operation, TIRED_RUNTIME_FAILED, "linger-enable-timeout",
                 "Lingering enablement timed out; a submitted change may still complete.");
        else
            fail(operation, TIRED_RUNTIME_FAILED, "linger-enable-error",
                 "Lingering enablement failed or its outcome is unknown.");
    }
    else if (sd_bus_message_has_signature(message, "") <= 0)
        fail(operation, TIRED_INVALID, "linger-enable-protocol",
             "Login manager returned an invalid enablement acknowledgement.");
    else
        operation->result.acknowledged = true;
    return 1;
}
bool tired_linger_enable_start(TiredManagerIdentity *identity, uid_t uid, bool interactive,
                               unsigned timeout_ms, TiredLingerEnable **output, TiredError *error)
{
    assert(identity != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.kind != TIRED_MANAGER_LOGIN || owner.error.status != TIRED_OK ||
        uid == (uid_t)-1 || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(
            error, TIRED_INVALID, "linger-enable-input",
            "Expected a ready login identity, account UID and bounded enablement deadline.", 0);
    TiredLingerEnable *operation = calloc(1, sizeof(*operation));
    if (operation == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate lingering enablement.", errno);
    operation->identity = identity;
    operation->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    operation->result.uid = uid;
    sd_bus_message *message = NULL;
    int rc;
    if (!now(&operation->deadline))
    {
        rc = -errno;
        goto failed;
    }
    operation->deadline += (uint64_t)timeout_ms * 1000U;
    rc = sd_bus_message_new_method_call(operation->bus, &message, owner.unique_name,
                                        "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                                        "SetUserLinger");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, interactive);
    if (rc >= 0)
        rc = sd_bus_message_append(message, "ubb", (uint32_t)uid, 1, interactive ? 1 : 0);
    if (rc >= 0)
        rc = sd_bus_call_async(operation->bus, &operation->slot, message, reply, operation,
                               (uint64_t)timeout_ms * 1000U);
    if (rc < 0)
        goto failed;
    operation->result.submitted = true;
    sd_bus_message_unref(message);
    *output = operation;
    tired_error_clear(error);
    return true;
failed:
    sd_bus_message_unref(message);
    tired_linger_enable_destroy(operation);
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "linger-enable-start",
                           "Cannot submit account lingering enablement.", -rc);
}
bool tired_linger_enable_step(TiredLingerEnable *operation)
{
    assert(operation != NULL);
    unsigned left;
    if (!operation->result.done && current(operation, &left))
    {
        if (operation->query == NULL)
        {
            (void)tired_manager_identity_step(operation->identity);
            if (current(operation, &left) && operation->result.acknowledged)
            {
                operation->slot = sd_bus_slot_unref(operation->slot);
                if (!tired_linger_query_start(operation->identity, operation->result.uid, left,
                                              &operation->query, &operation->result.error))
                    operation->result.done = true;
            }
        }
        else if (tired_linger_query_step(operation->query) && current(operation, &left))
        {
            TiredLingerResult observed = tired_linger_query_result(operation->query);
            operation->result.done = true;
            operation->result.error = observed.error;
            operation->result.observed = observed.known;
            operation->result.enabled = observed.enabled;
            if (observed.error.status == TIRED_OK && (!observed.known || !observed.enabled))
                fail(operation, TIRED_RUNTIME_FAILED, "linger-enable-unconfirmed",
                     "Account lingering was not observed enabled after acknowledgement.");
        }
    }
    if (operation->result.done)
    {
        operation->slot = sd_bus_slot_unref(operation->slot);
        tired_linger_query_destroy(operation->query);
        operation->query = NULL;
    }
    return operation->result.done;
}
bool tired_linger_enable_poll(TiredLingerEnable *operation, struct pollfd *descriptor,
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
    bool ok =
        operation->query == NULL
            ? tired_manager_identity_poll(operation->identity, descriptor, deadline_usec, error)
            : tired_linger_query_poll(operation->query, descriptor, deadline_usec, error);
    if (ok && *deadline_usec > operation->deadline)
        *deadline_usec = operation->deadline;
    return ok;
}
void tired_linger_enable_cancel(TiredLingerEnable *operation)
{
    assert(operation != NULL);
    if (!operation->result.done)
    {
        fail(operation, TIRED_CANCELLED, "linger-enable-cancelled",
             "Lingering enablement wait cancelled; a submitted change may still complete.");
        operation->slot = sd_bus_slot_unref(operation->slot);
        tired_linger_query_destroy(operation->query);
        operation->query = NULL;
    }
}
TiredLingerEnableResult tired_linger_enable_result(const TiredLingerEnable *operation)
{
    assert(operation != NULL);
    TiredLingerEnableResult result = operation->result;
    TiredManagerIdentityResult owner = tired_manager_identity_result(operation->identity);
    if (owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
        result.observed = result.enabled = false;
    }
    return result;
}
void tired_linger_enable_destroy(TiredLingerEnable *operation)
{
    if (operation == NULL)
        return;
    sd_bus_slot_unref(operation->slot);
    tired_linger_query_destroy(operation->query);
    sd_bus_unref(operation->bus);
    free(operation);
}
