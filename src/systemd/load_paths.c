#include "tired/load_paths.h"
#include "tired/capture.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct TiredLoadPaths
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *slot;
    TiredTextList paths;
    uint64_t deadline;
    bool done;
    TiredError error;
};
static bool protocol(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "manager-paths-protocol",
                           "Manager UnitPath must be a bounded, nonempty array of absolute paths.",
                           0);
}
bool tired_load_paths_read(sd_bus_message *message, TiredTextList *output, TiredError *error)
{
    assert(message != NULL && output != NULL);
    TiredTextList paths = {0};
    bool ok = false;
    if (sd_bus_message_has_signature(message, "v") <= 0 ||
        sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "as") <= 0 ||
        sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "s") <= 0)
    {
        protocol(error);
        goto done;
    }
    for (;;)
    {
        const char *path = NULL;
        int rc = sd_bus_message_read_basic(message, 's', &path);
        if (rc < 0)
        {
            protocol(error);
            goto done;
        }
        if (rc == 0)
            break;
        size_t length = strnlen(path, 4097);
        if (length == 0 || length > 4096 || path[0] != '/' ||
            !tired_validate_text(path, length, true, error))
        {
            protocol(error);
            goto done;
        }
        if (!tired_text_list_append(&paths, path, length, 256, TIRED_INPUT_LIMIT, error))
            goto done;
    }
    if (paths.count == 0 || sd_bus_message_exit_container(message) < 0 ||
        sd_bus_message_exit_container(message) < 0 || sd_bus_message_at_end(message, true) <= 0)
    {
        protocol(error);
        goto done;
    }
    tired_text_list_destroy(output);
    *output = paths;
    paths = (TiredTextList){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_text_list_destroy(&paths);
    return ok;
}
static bool now_usec(uint64_t *now)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *now = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void fail(TiredLoadPaths *query, TiredStatus status, const char *code, const char *message)
{
    query->done = true;
    tired_error_set(&query->error, status, code, message, 0);
}
static bool current(TiredLoadPaths *query)
{
    TiredManagerIdentityResult identity = tired_manager_identity_result(query->identity);
    if (!identity.ready)
    {
        query->done = true;
        query->error = identity.error;
        if (query->error.status == TIRED_OK)
            fail(query, TIRED_CONFLICT, "manager-paths-owner", "Manager identity is not ready.");
        return false;
    }
    uint64_t now;
    if (!now_usec(&now))
        fail(query, TIRED_INTERNAL, "manager-paths-clock", "Cannot read load-path query clock.");
    else if (now >= query->deadline)
        fail(query, TIRED_RUNTIME_FAILED, "manager-paths-timeout",
             "Manager load-path query timed out.");
    return !query->done;
}
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    TiredLoadPaths *query = userdata;
    if (query->done || !current(query))
        return 1;
    const sd_bus_error *remote = sd_bus_message_get_error(message);
    if (remote != NULL)
    {
        if (sd_bus_error_has_name(remote, SD_BUS_ERROR_ACCESS_DENIED) ||
            sd_bus_error_has_name(remote, SD_BUS_ERROR_AUTH_FAILED))
            fail(query, TIRED_AUTHORIZATION, "manager-paths-authorization",
                 "Manager denied load-path discovery.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_PROPERTY) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_UNKNOWN_INTERFACE))
            fail(query, TIRED_UNSUPPORTED, "manager-paths-unsupported",
                 "Manager UnitPath is unavailable.");
        else if (sd_bus_is_open(query->bus) <= 0)
            fail(query, TIRED_RUNTIME_FAILED, "manager-disconnected",
                 "Manager disconnected during load-path discovery.");
        else if (sd_bus_error_has_name(remote, SD_BUS_ERROR_NO_REPLY) ||
                 sd_bus_error_has_name(remote, SD_BUS_ERROR_TIMEOUT))
            fail(query, TIRED_RUNTIME_FAILED, "manager-paths-timeout",
                 "Manager load-path query timed out.");
        else
            fail(query, TIRED_RUNTIME_FAILED, "manager-paths-error",
                 "Manager returned a load-path query error.");
    }
    else
    {
        query->done = true;
        (void)tired_load_paths_read(message, &query->paths, &query->error);
    }
    return 1;
}
bool tired_load_paths_start(TiredManagerIdentity *identity, unsigned timeout_ms,
                            TiredLoadPaths **output, TiredError *error)
{
    assert(identity != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.error.status != TIRED_OK || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(error, TIRED_INVALID, "manager-paths-input",
                               "Load-path query requires a ready identity and bounded deadline.",
                               0);
    TiredLoadPaths *query = calloc(1, sizeof(*query));
    if (query == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate load-path query.", errno);
    query->identity = identity;
    query->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    sd_bus_message *message = NULL;
    int rc = -EIO;
    if (!now_usec(&query->deadline))
    {
        rc = -errno;
        goto failed;
    }
    query->deadline += (uint64_t)timeout_ms * 1000;
    rc = sd_bus_message_new_method_call(query->bus, &message, owner.unique_name,
                                        "/org/freedesktop/systemd1",
                                        "org.freedesktop.DBus.Properties", "Get");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_append(message, "ss", "org.freedesktop.systemd1.Manager", "UnitPath");
    if (rc >= 0)
        rc = sd_bus_call_async(query->bus, &query->slot, message, reply, query,
                               (uint64_t)timeout_ms * 1000);
    if (rc < 0)
        goto failed;
    sd_bus_message_unref(message);
    *output = query;
    tired_error_clear(error);
    return true;
failed:
    sd_bus_message_unref(message);
    tired_load_paths_destroy(query);
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "manager-paths-start",
                           "Cannot queue manager load-path discovery.", -rc);
}
bool tired_load_paths_step(TiredLoadPaths *query)
{
    assert(query != NULL);
    if (!query->done && current(query))
    {
        (void)tired_manager_identity_step(query->identity);
        TiredManagerIdentityResult identity = tired_manager_identity_result(query->identity);
        if (identity.error.status != TIRED_OK)
        {
            query->done = true;
            query->error = identity.error;
        }
    }
    if (query->done)
        query->slot = sd_bus_slot_unref(query->slot);
    return query->done;
}
bool tired_load_paths_poll(TiredLoadPaths *query, struct pollfd *descriptor,
                           uint64_t *deadline_usec, TiredError *error)
{
    assert(query != NULL && descriptor != NULL && deadline_usec != NULL);
    if (query->done)
    {
        *descriptor = (struct pollfd){.fd = -1};
        *deadline_usec = UINT64_MAX;
        tired_error_clear(error);
        return true;
    }
    if (!tired_manager_identity_poll(query->identity, descriptor, deadline_usec, error))
        return false;
    if (query->deadline < *deadline_usec)
        *deadline_usec = query->deadline;
    return true;
}
void tired_load_paths_cancel(TiredLoadPaths *query)
{
    assert(query != NULL);
    if (!query->done)
    {
        fail(query, TIRED_CANCELLED, "manager-paths-cancelled",
             "Load-path discovery was cancelled.");
        query->slot = sd_bus_slot_unref(query->slot);
    }
}
TiredLoadPathsResult tired_load_paths_result(const TiredLoadPaths *query)
{
    assert(query != NULL);
    TiredLoadPathsResult result = {.done = query->done, .error = query->error};
    TiredManagerIdentityResult identity = tired_manager_identity_result(query->identity);
    if (result.error.status == TIRED_OK && identity.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = identity.error;
    }
    if (result.done && result.error.status == TIRED_OK)
        result.directories = &query->paths;
    return result;
}
void tired_load_paths_destroy(TiredLoadPaths *query)
{
    if (query == NULL)
        return;
    sd_bus_slot_unref(query->slot);
    sd_bus_unref(query->bus);
    tired_text_list_destroy(&query->paths);
    free(query);
}
