#include "tired/manager_identity.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct TiredManagerIdentity
{
    sd_bus *bus;
    sd_bus_slot *watch, *request;
    TiredText owner;
    uid_t expected, uid;
    uint64_t deadline;
    bool ready, changed;
    TiredError error;
};
static bool now_usec(uint64_t *value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return false;
    *value = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
    return true;
}
static void fail(TiredManagerIdentity *identity, TiredStatus status, const char *code,
                 const char *message)
{
    identity->ready = false;
    tired_error_set(&identity->error, status, code, message, 0);
}
static bool remote_failure(TiredManagerIdentity *identity, sd_bus_message *message)
{
    const sd_bus_error *error = sd_bus_message_get_error(message);
    if (error == NULL)
        return false;
    if (sd_bus_error_has_name(error, SD_BUS_ERROR_ACCESS_DENIED) ||
        sd_bus_error_has_name(error, SD_BUS_ERROR_AUTH_FAILED))
        fail(identity, TIRED_AUTHORIZATION, "manager-owner-authorization",
             "Cannot inspect manager owner credentials.");
    else if (sd_bus_error_has_name(error, SD_BUS_ERROR_NAME_HAS_NO_OWNER) ||
             sd_bus_error_has_name(error, SD_BUS_ERROR_SERVICE_UNKNOWN))
        fail(identity, TIRED_NOT_FOUND, "manager-unavailable",
             "No manager owns the requested bus name.");
    else if (sd_bus_is_open(identity->bus) <= 0)
        fail(identity, TIRED_RUNTIME_FAILED, "manager-disconnected",
             "Manager bus disconnected during identity discovery.");
    else if (sd_bus_error_has_name(error, SD_BUS_ERROR_NO_REPLY) ||
             sd_bus_error_has_name(error, SD_BUS_ERROR_TIMEOUT))
        fail(identity, TIRED_RUNTIME_FAILED, "manager-owner-timeout",
             "Manager identity discovery timed out.");
    else
        fail(identity, TIRED_RUNTIME_FAILED, "manager-owner-error",
             "Broker rejected manager identity discovery.");
    return true;
}
static bool queue(TiredManagerIdentity *identity, const char *method, const char *argument,
                  sd_bus_message_handler_t callback)
{
    uint64_t now;
    if (!now_usec(&now))
    {
        fail(identity, TIRED_INTERNAL, "manager-clock", "Cannot read identity discovery clock.");
        return false;
    }
    if (now >= identity->deadline)
    {
        fail(identity, TIRED_RUNTIME_FAILED, "manager-owner-timeout",
             "Manager identity discovery timed out.");
        return false;
    }
    sd_bus_message *message = NULL;
    int rc =
        sd_bus_message_new_method_call(identity->bus, &message, "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus", "org.freedesktop.DBus", method);
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_append(message, "s", argument);
    identity->request = sd_bus_slot_unref(identity->request);
    if (rc >= 0)
        rc = sd_bus_call_async(identity->bus, &identity->request, message, callback, identity,
                               identity->deadline - now);
    sd_bus_message_unref(message);
    if (rc < 0)
    {
        fail(identity, TIRED_RUNTIME_FAILED, "manager-owner-io",
             "Cannot queue manager identity query.");
        identity->error.system_errno = -rc;
        return false;
    }
    return true;
}
static int uid_reply(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    TiredManagerIdentity *identity = userdata;
    if (identity->error.status != TIRED_OK || remote_failure(identity, message))
        return 1;
    uint32_t uid;
    if (sd_bus_message_has_signature(message, "u") <= 0 ||
        sd_bus_message_read(message, "u", &uid) <= 0)
        fail(identity, TIRED_INVALID, "manager-owner-protocol",
             "Broker returned an invalid manager UID.");
    else if ((uid_t)uid != identity->expected)
        fail(identity, TIRED_AUTHORIZATION, "manager-owner-uid",
             "Manager bus name belongs to an unexpected UID.");
    else
    {
        identity->uid = (uid_t)uid;
        identity->ready = true;
    }
    return 1;
}
static int owner_reply(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    TiredManagerIdentity *identity = userdata;
    if (identity->error.status != TIRED_OK || remote_failure(identity, message))
        return 1;
    const char *owner = NULL;
    if (sd_bus_message_has_signature(message, "s") <= 0 ||
        sd_bus_message_read(message, "s", &owner) <= 0 || owner[0] != ':' ||
        strnlen(owner, 256) > 255 || sd_bus_service_name_is_valid(owner) <= 0)
    {
        fail(identity, TIRED_INVALID, "manager-owner-protocol",
             "Broker returned an invalid unique manager name.");
        return 1;
    }
    if (!tired_text_set(&identity->owner, owner, strlen(owner), 255, &identity->error))
        return 1;
    (void)queue(identity, "GetConnectionUnixUser", identity->owner.data, uid_reply);
    return 1;
}
static int match_installed(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    TiredManagerIdentity *identity = userdata;
    if (identity->error.status != TIRED_OK || remote_failure(identity, message))
        return 1;
    if (sd_bus_message_has_signature(message, "") <= 0)
        fail(identity, TIRED_INVALID, "manager-owner-protocol",
             "Broker returned an invalid match acknowledgment.");
    else
        (void)queue(identity, "GetNameOwner", "org.freedesktop.systemd1", owner_reply);
    return 1;
}
static int owner_changed(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    TiredManagerIdentity *identity = userdata;
    const char *name = NULL, *previous = NULL, *next = NULL;
    if (identity->error.status != TIRED_OK)
        return 0;
    if (sd_bus_message_has_signature(message, "sss") <= 0 ||
        sd_bus_message_read(message, "sss", &name, &previous, &next) <= 0 ||
        strnlen(name, 256) > 255 || strnlen(previous, 256) > 255 || strnlen(next, 256) > 255 ||
        (previous[0] != '\0' &&
         (previous[0] != ':' || sd_bus_service_name_is_valid(previous) <= 0)) ||
        (next[0] != '\0' && (next[0] != ':' || sd_bus_service_name_is_valid(next) <= 0)))
        fail(identity, TIRED_INVALID, "manager-owner-protocol",
             "Broker returned an invalid owner-change signal.");
    else if (strcmp(name, "org.freedesktop.systemd1") == 0 && strcmp(previous, next) != 0)
    {
        identity->changed = true;
        fail(identity, TIRED_CONFLICT, "manager-owner-changed",
             "Manager ownership changed; repeat discovery before continuing.");
    }
    return 0; /* Other observers must also see invalidation. */
}
bool tired_manager_identity_start(sd_bus *bus, bool user_scope, unsigned timeout_ms,
                                  TiredManagerIdentity **output, TiredError *error)
{
    assert(bus != NULL && output != NULL && *output == NULL);
    if (timeout_ms == 0 || timeout_ms > 300000 || sd_bus_is_open(bus) <= 0 ||
        sd_bus_is_bus_client(bus) <= 0)
        return tired_error_set(error, TIRED_INVALID, "manager-owner-input",
                               "Expected a broker connection and bounded identity deadline.", 0);
    TiredManagerIdentity *identity = calloc(1, sizeof(*identity));
    if (identity == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate manager identity.", errno);
    identity->bus = sd_bus_ref(bus);
    identity->expected = user_scope ? getuid() : 0;
    if (!now_usec(&identity->deadline))
    {
        tired_manager_identity_destroy(identity);
        return tired_error_set(error, TIRED_INTERNAL, "manager-clock",
                               "Cannot read identity discovery clock.", errno);
    }
    identity->deadline += (uint64_t)timeout_ms * 1000;
    int rc = sd_bus_add_match_async(
        bus, &identity->watch,
        "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',"
        "interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.freedesktop."
        "systemd1'",
        owner_changed, match_installed, identity);
    if (rc < 0)
    {
        tired_manager_identity_destroy(identity);
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "manager-owner-watch",
                               "Cannot subscribe to manager ownership changes.", -rc);
    }
    *output = identity;
    tired_error_clear(error);
    return true;
}
bool tired_manager_identity_step(TiredManagerIdentity *identity)
{
    assert(identity != NULL);
    if (identity->error.status == TIRED_OK && !identity->ready)
    {
        uint64_t now;
        if (!now_usec(&now))
            fail(identity, TIRED_INTERNAL, "manager-clock",
                 "Cannot read identity discovery clock.");
        else if (now >= identity->deadline)
            fail(identity, TIRED_RUNTIME_FAILED, "manager-owner-timeout",
                 "Manager identity discovery timed out.");
    }
    for (unsigned i = 0; identity->error.status == TIRED_OK && i < 16; ++i)
    {
        int rc = sd_bus_process(identity->bus, NULL);
        if (rc < 0)
        {
            fail(identity, TIRED_RUNTIME_FAILED, "manager-disconnected",
                 "Manager bus disconnected.");
            identity->error.system_errno = -rc;
        }
        if (rc <= 0)
            break;
    }
    if (identity->error.status != TIRED_OK)
    {
        identity->request = sd_bus_slot_unref(identity->request);
        identity->watch = sd_bus_slot_unref(identity->watch);
    }
    else if (identity->ready)
        identity->request = sd_bus_slot_unref(identity->request);
    return identity->ready || identity->error.status != TIRED_OK;
}
bool tired_manager_identity_poll(TiredManagerIdentity *identity, struct pollfd *descriptor,
                                 uint64_t *deadline_usec, TiredError *error)
{
    assert(identity != NULL && descriptor != NULL && deadline_usec != NULL);
    if (identity->error.status != TIRED_OK)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    int fd = sd_bus_get_fd(identity->bus), events = sd_bus_get_events(identity->bus);
    uint64_t deadline;
    int rc = sd_bus_get_timeout(identity->bus, &deadline);
    if (fd < 0 || events < 0 || rc < 0)
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "manager-owner-poll",
                               "Cannot obtain manager ownership polling state.",
                               fd < 0       ? -fd
                               : events < 0 ? -events
                                            : -rc);
    *descriptor = (struct pollfd){.fd = fd, .events = (short)events};
    *deadline_usec =
        !identity->ready && identity->deadline < deadline ? identity->deadline : deadline;
    tired_error_clear(error);
    return true;
}
TiredManagerIdentityResult tired_manager_identity_result(const TiredManagerIdentity *identity)
{
    assert(identity != NULL);
    return (TiredManagerIdentityResult){.ready = identity->ready,
                                        .changed = identity->changed,
                                        .unique_name = identity->owner.data,
                                        .uid = identity->uid,
                                        .error = identity->error};
}
void tired_manager_identity_destroy(TiredManagerIdentity *identity)
{
    if (identity == NULL)
        return;
    sd_bus_slot_unref(identity->request);
    sd_bus_slot_unref(identity->watch);
    sd_bus_unref(identity->bus);
    tired_text_destroy(&identity->owner);
    free(identity);
}
