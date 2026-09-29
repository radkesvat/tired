#include "tired/linger_query.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct TiredLingerQuery
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *slot;
    TiredText path;
    uint64_t deadline;
    unsigned stage;
    TiredLingerResult result;
};
static bool now(uint64_t *usec)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *usec = (uint64_t)time.tv_sec * 1000000U + (uint64_t)time.tv_nsec / 1000U;
    return true;
}
static void fail(TiredLingerQuery *query, TiredStatus status, const char *code, const char *message)
{
    query->result.done = true;
    query->result.known = query->result.enabled = false;
    tired_error_set(&query->result.error, status, code, message, 0);
}
static bool current(TiredLingerQuery *query)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    uint64_t time;
    if (!owner.ready || owner.error.status != TIRED_OK)
    {
        fail(query, TIRED_CONFLICT, "linger-owner", "Login manager identity is no longer ready.");
        if (owner.error.status != TIRED_OK)
            query->result.error = owner.error;
    }
    else if (!now(&time))
        fail(query, TIRED_INTERNAL, "linger-clock", "Cannot read lingering query clock.");
    else if (time >= query->deadline)
        fail(query, TIRED_RUNTIME_FAILED, "linger-timeout", "Lingering observation timed out.");
    return !query->result.done;
}
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *unused);
static bool queue(TiredLingerQuery *query)
{
    if (!current(query))
        return false;
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    uint64_t time;
    if (!now(&time))
    {
        fail(query, TIRED_INTERNAL, "linger-clock", "Cannot read lingering query clock.");
        return false;
    }
    if (time >= query->deadline)
    {
        fail(query, TIRED_RUNTIME_FAILED, "linger-timeout", "Lingering observation timed out.");
        return false;
    }
    sd_bus_message *message = NULL;
    int rc = sd_bus_message_new_method_call(
        query->bus, &message, owner.unique_name,
        query->stage == 0 ? "/org/freedesktop/login1" : query->path.data,
        query->stage == 0 ? "org.freedesktop.login1.Manager" : "org.freedesktop.DBus.Properties",
        query->stage == 0 ? "GetUser" : "Get");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0)
        rc = query->stage == 0 ? sd_bus_message_append(message, "u", (uint32_t)query->result.uid)
                               : sd_bus_message_append(message, "ss", "org.freedesktop.login1.User",
                                                       query->stage == 1 ? "UID" : "Linger");
    query->slot = sd_bus_slot_unref(query->slot);
    if (rc >= 0)
        rc = sd_bus_call_async(query->bus, &query->slot, message, reply, query,
                               query->deadline - time);
    sd_bus_message_unref(message);
    if (rc < 0)
    {
        fail(query, TIRED_RUNTIME_FAILED, "linger-io", "Cannot queue lingering observation.");
        query->result.error.system_errno = -rc;
    }
    return rc >= 0;
}
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *unused)
{
    (void)unused;
    TiredLingerQuery *query = userdata;
    if (query->result.done || !current(query))
        return 1;
    const sd_bus_error *remote = sd_bus_message_get_error(message);
    if (remote != NULL)
    {
        if (sd_bus_error_has_name(remote, SD_BUS_ERROR_ACCESS_DENIED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_AUTH_FAILED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_INTERACTIVE_AUTHORIZATION_REQUIRED))
            fail(query, TIRED_AUTHORIZATION, "linger-authorization",
                 "Login manager denied account observation.");
        else if (sd_bus_error_has_name(remote, "org.freedesktop.login1.NoSuchUser") ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_OBJECT))
            fail(query, TIRED_NOT_FOUND, "linger-user-unavailable",
                 "Login manager has no observable object for this account.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_PROPERTY) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_METHOD) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_INTERFACE))
            fail(query, TIRED_UNSUPPORTED, "linger-unsupported",
                 "Login manager account properties are unavailable.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_NO_REPLY) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_TIMEOUT))
            fail(query, TIRED_RUNTIME_FAILED, "linger-timeout", "Lingering observation timed out.");
        else
            fail(query, TIRED_RUNTIME_FAILED, "linger-error",
                 "Login manager could not complete account observation.");
        return 1;
    }
    if (query->stage == 0)
    {
        const char *path = NULL;
        if (sd_bus_message_has_signature(message, "o") <= 0 ||
            sd_bus_message_read(message, "o", &path) <= 0 || strnlen(path, 4097) > 4096 ||
            sd_bus_object_path_is_valid(path) <= 0)
            goto protocol;
        if (!tired_text_set(&query->path, path, strlen(path), 4096, &query->result.error))
        {
            query->result.done = true;
            return 1;
        }
    }
    else
    {
        if (sd_bus_message_has_signature(message, "v") <= 0 ||
            sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT,
                                           query->stage == 1 ? "u" : "b") <= 0)
            goto protocol;
        if (query->stage == 1)
        {
            uint32_t uid;
            if (sd_bus_message_read(message, "u", &uid) <= 0)
                goto protocol;
            if ((uid_t)uid != query->result.uid)
            {
                fail(query, TIRED_CONFLICT, "linger-uid",
                     "Login manager returned a different account UID.");
                return 1;
            }
        }
        else
        {
            int enabled;
            if (sd_bus_message_read(message, "b", &enabled) <= 0)
                goto protocol;
            query->result.enabled = enabled != 0;
        }
        if (sd_bus_message_exit_container(message) < 0)
            goto protocol;
    }
    if (sd_bus_message_at_end(message, true) <= 0)
        goto protocol;
    if (++query->stage == 3)
        query->result.done = query->result.known = true;
    else
        (void)queue(query);
    return 1;
protocol:
    fail(query, TIRED_INVALID, "linger-protocol",
         "Login manager returned invalid account property data.");
    return 1;
}
bool tired_linger_query_start(TiredManagerIdentity *identity, uid_t uid, unsigned timeout_ms,
                              TiredLingerQuery **output, TiredError *error)
{
    assert(identity != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.kind != TIRED_MANAGER_LOGIN || owner.error.status != TIRED_OK ||
        uid == (uid_t)-1 || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(error, TIRED_INVALID, "linger-input",
                               "Expected a ready login identity, account UID and bounded deadline.",
                               0);
    TiredLingerQuery *query = calloc(1, sizeof(*query));
    if (query == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate lingering query.", errno);
    query->identity = identity;
    query->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    query->result.uid = uid;
    if (!now(&query->deadline))
    {
        fail(query, TIRED_INTERNAL, "linger-clock", "Cannot read lingering query clock.");
        goto failed;
    }
    query->deadline += (uint64_t)timeout_ms * 1000U;
    if (!queue(query))
        goto failed;
    *output = query;
    tired_error_clear(error);
    return true;
failed:
    if (error != NULL)
        *error = query->result.error;
    tired_linger_query_destroy(query);
    return false;
}
bool tired_linger_query_step(TiredLingerQuery *query)
{
    assert(query != NULL);
    if (!query->result.done && current(query))
    {
        (void)tired_manager_identity_step(query->identity);
        TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
        if (owner.error.status != TIRED_OK)
        {
            fail(query, owner.error.status, owner.error.code, owner.error.message);
            query->result.error = owner.error;
        }
    }
    if (query->result.done)
        query->slot = sd_bus_slot_unref(query->slot);
    return query->result.done;
}
bool tired_linger_query_poll(TiredLingerQuery *query, struct pollfd *descriptor,
                             uint64_t *deadline_usec, TiredError *error)
{
    assert(query != NULL && descriptor != NULL && deadline_usec != NULL);
    if (query->result.done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    if (!tired_manager_identity_poll(query->identity, descriptor, deadline_usec, error))
        return false;
    if (*deadline_usec > query->deadline)
        *deadline_usec = query->deadline;
    return true;
}
void tired_linger_query_cancel(TiredLingerQuery *query)
{
    assert(query != NULL);
    if (!query->result.done)
    {
        fail(query, TIRED_CANCELLED, "linger-cancelled", "Lingering observation was cancelled.");
        query->slot = sd_bus_slot_unref(query->slot);
    }
}
TiredLingerResult tired_linger_query_result(const TiredLingerQuery *query)
{
    assert(query != NULL);
    TiredLingerResult result = query->result;
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    if (owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
        result.known = result.enabled = false;
    }
    if (!result.done || result.error.status != TIRED_OK)
        result.known = result.enabled = false;
    return result;
}
void tired_linger_query_destroy(TiredLingerQuery *query)
{
    if (query == NULL)
        return;
    sd_bus_slot_unref(query->slot);
    sd_bus_unref(query->bus);
    tired_text_destroy(&query->path);
    free(query);
}
