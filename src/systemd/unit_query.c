#include "tired/unit_query.h"
#include "tired/capture.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum
{
    FILE_STATE,
    OBJECT_PATH,
    UNIT_PROPERTIES,
    SERVICE_PROPERTIES
} Phase;
struct TiredUnitQuery
{
    TiredManagerIdentity *identity;
    sd_bus *bus;
    sd_bus_slot *slot;
    TiredText owner, name, file_state, object_path;
    TiredUnitObservation observation;
    TiredError error;
    uint64_t deadline;
    Phase phase;
    bool done, file_found, object_found;
};
static bool now_usec(uint64_t *now)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return false;
    *now = (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
    return true;
}
static void fail(TiredUnitQuery *query, TiredStatus status, const char *code, const char *message)
{
    query->done = true;
    tired_error_set(&query->error, status, code, message, 0);
}
static bool identity_ready(TiredUnitQuery *query)
{
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    if (owner.ready && owner.error.status == TIRED_OK &&
        strcmp(owner.unique_name, query->owner.data) == 0)
        return true;
    query->done = true;
    if (owner.error.status != TIRED_OK)
        query->error = owner.error;
    else
        fail(query, TIRED_CONFLICT, "unit-query-owner", "Manager identity is no longer ready.");
    return false;
}
static bool remote_failure(TiredUnitQuery *query, sd_bus_message *message)
{
    const sd_bus_error *error = sd_bus_message_get_error(message);
    if (error == NULL)
        return false;
    if (sd_bus_error_has_name(error, SD_BUS_ERROR_ACCESS_DENIED) ||
        sd_bus_error_has_name(error, SD_BUS_ERROR_AUTH_FAILED))
        fail(query, TIRED_AUTHORIZATION, "unit-query-authorization",
             "Manager denied the unit query.");
    else if (sd_bus_is_open(query->bus) <= 0 ||
             sd_bus_error_has_name(error, SD_BUS_ERROR_DISCONNECTED))
        fail(query, TIRED_RUNTIME_FAILED, "manager-disconnected",
             "Manager disconnected during the unit query.");
    else if (sd_bus_error_has_name(error, SD_BUS_ERROR_NO_REPLY) ||
             sd_bus_error_has_name(error, SD_BUS_ERROR_TIMEOUT))
        fail(query, TIRED_RUNTIME_FAILED, "unit-query-timeout", "Unit query timed out.");
    else if (sd_bus_error_has_name(error, SD_BUS_ERROR_UNKNOWN_OBJECT) ||
             sd_bus_error_has_name(error, "org.freedesktop.systemd1.NoSuchUnit"))
        fail(query, TIRED_CONFLICT, "unit-query-changed",
             "Unit object disappeared during observation.");
    else if (sd_bus_error_has_name(error, SD_BUS_ERROR_UNKNOWN_METHOD) ||
             sd_bus_error_has_name(error, SD_BUS_ERROR_UNKNOWN_INTERFACE))
        fail(query, TIRED_UNSUPPORTED, "unit-query-unsupported",
             "Manager does not support the requested unit query.");
    else
        fail(query, TIRED_RUNTIME_FAILED, "unit-query-error",
             "Manager returned a unit query error.");
    return true;
}
static bool queue(TiredUnitQuery *query, Phase phase);
static int reply(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    TiredUnitQuery *query = userdata;
    if (query->done || !identity_ready(query))
        return 1;
    uint64_t now;
    if (!now_usec(&now))
    {
        fail(query, TIRED_INTERNAL, "unit-query-clock", "Cannot read unit query clock.");
        return 1;
    }
    if (now >= query->deadline)
    {
        fail(query, TIRED_RUNTIME_FAILED, "unit-query-timeout", "Unit query timed out.");
        return 1;
    }
    if (query->phase == FILE_STATE &&
        sd_bus_message_is_method_error(message, SD_BUS_ERROR_FILE_NOT_FOUND) > 0)
    {
        (void)queue(query, OBJECT_PATH);
        return 1;
    }
    if (query->phase == OBJECT_PATH &&
        sd_bus_message_is_method_error(message, "org.freedesktop.systemd1.NoSuchUnit") > 0)
    {
        query->done = true;
        return 1;
    }
    if (remote_failure(query, message))
        return 1;
    if (query->phase == FILE_STATE || query->phase == OBJECT_PATH)
    {
        bool file = query->phase == FILE_STATE;
        const char *text = NULL;
        const char *signature = file ? "s" : "o";
        size_t limit = file ? 256 : 4096;
        if (sd_bus_message_has_signature(message, signature) <= 0 ||
            sd_bus_message_read_basic(message, signature[0], &text) <= 0 ||
            strnlen(text, limit + 1) > limit || text[0] == '\0' ||
            !tired_validate_text(text, strlen(text), true, &query->error))
        {
            fail(query, TIRED_INVALID, "unit-query-protocol",
                 "Manager returned an invalid unit state or path.");
            return 1;
        }
        static const char prefix[] = "/org/freedesktop/systemd1/unit/";
        if (!file &&
            (strncmp(text, prefix, sizeof(prefix) - 1) != 0 || text[sizeof(prefix) - 1] == '\0'))
        {
            fail(query, TIRED_INVALID, "unit-query-protocol",
                 "Manager returned a path outside its unit objects.");
            return 1;
        }
        if (!tired_text_set(file ? &query->file_state : &query->object_path, text, strlen(text),
                            limit, &query->error))
        {
            query->done = true;
            return 1;
        }
        if (file)
            query->file_found = true;
        else
            query->object_found = true;
        (void)queue(query, file ? OBJECT_PATH : UNIT_PROPERTIES);
    }
    else
    {
        if (!tired_observation_read(message,
                                    query->phase == UNIT_PROPERTIES ? TIRED_OBSERVE_UNIT
                                                                    : TIRED_OBSERVE_SERVICE,
                                    &query->observation, &query->error))
        {
            query->done = true;
            return 1;
        }
        if (query->phase == UNIT_PROPERTIES)
            (void)queue(query, SERVICE_PROPERTIES);
        else
            query->done = true;
    }
    return 1;
}
static bool queue(TiredUnitQuery *query, Phase phase)
{
    if (!identity_ready(query))
        return false;
    uint64_t now;
    if (!now_usec(&now))
    {
        fail(query, TIRED_INTERNAL, "unit-query-clock", "Cannot read unit query clock.");
        return false;
    }
    if (now >= query->deadline)
    {
        fail(query, TIRED_RUNTIME_FAILED, "unit-query-timeout", "Unit query timed out.");
        return false;
    }
    bool properties = phase == UNIT_PROPERTIES || phase == SERVICE_PROPERTIES;
    sd_bus_message *message = NULL;
    int rc = sd_bus_message_new_method_call(
        query->bus, &message, query->owner.data,
        properties ? query->object_path.data : "/org/freedesktop/systemd1",
        properties ? "org.freedesktop.DBus.Properties" : "org.freedesktop.systemd1.Manager",
        properties            ? "GetAll"
        : phase == FILE_STATE ? "GetUnitFileState"
                              : "GetUnit");
    if (rc >= 0)
        rc = sd_bus_message_set_auto_start(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_set_allow_interactive_authorization(message, 0);
    if (rc >= 0)
        rc = sd_bus_message_append(message, "s",
                                   !properties                ? query->name.data
                                   : phase == UNIT_PROPERTIES ? "org.freedesktop.systemd1.Unit"
                                                              : "org.freedesktop.systemd1.Service");
    query->slot = sd_bus_slot_unref(query->slot);
    query->phase = phase;
    if (rc >= 0)
        rc = sd_bus_call_async(query->bus, &query->slot, message, reply, query,
                               query->deadline - now);
    sd_bus_message_unref(message);
    if (rc < 0)
    {
        fail(query, TIRED_RUNTIME_FAILED, "unit-query-io", "Cannot queue unit observation.");
        query->error.system_errno = -rc;
        return false;
    }
    return true;
}
bool tired_unit_query_start(TiredManagerIdentity *identity, const TiredText *base,
                            unsigned timeout_ms, TiredUnitQuery **output, TiredError *error)
{
    assert(identity != NULL && base != NULL && output != NULL && *output == NULL);
    TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
    if (!owner.ready || owner.error.status != TIRED_OK || timeout_ms == 0 || timeout_ms > 300000)
        return tired_error_set(error, TIRED_INVALID, "unit-query-input",
                               "Unit query requires a ready manager identity and bounded deadline.",
                               0);
    TiredUnitQuery *query = calloc(1, sizeof(*query));
    if (query == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate unit query.",
                               errno);
    query->identity = identity;
    query->bus = sd_bus_ref(tired_manager_identity_bus(identity));
    if (!tired_name_candidate(base, 1, &query->name, error) ||
        !tired_text_set(&query->owner, owner.unique_name, strlen(owner.unique_name), 255, error))
        goto fail;
    if (!now_usec(&query->deadline))
    {
        tired_error_set(error, TIRED_INTERNAL, "unit-query-clock", "Cannot read unit query clock.",
                        errno);
        goto fail;
    }
    query->deadline += (uint64_t)timeout_ms * 1000;
    if (!queue(query, FILE_STATE))
    {
        if (error != NULL)
            *error = query->error;
        goto fail;
    }
    *output = query;
    tired_error_clear(error);
    return true;
fail:
    tired_unit_query_destroy(query);
    return false;
}
bool tired_unit_query_step(TiredUnitQuery *query)
{
    assert(query != NULL);
    if (!query->done)
    {
        uint64_t now;
        if (!now_usec(&now))
            fail(query, TIRED_INTERNAL, "unit-query-clock", "Cannot read unit query clock.");
        else if (now >= query->deadline)
            fail(query, TIRED_RUNTIME_FAILED, "unit-query-timeout", "Unit query timed out.");
        else
        {
            (void)tired_manager_identity_step(query->identity);
            (void)identity_ready(query);
        }
    }
    if (query->done)
        query->slot = sd_bus_slot_unref(query->slot);
    return query->done;
}
bool tired_unit_query_poll(TiredUnitQuery *query, struct pollfd *descriptor,
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
void tired_unit_query_cancel(TiredUnitQuery *query)
{
    assert(query != NULL);
    if (!query->done)
    {
        fail(query, TIRED_CANCELLED, "unit-query-cancelled", "Unit observation was cancelled.");
        query->slot = sd_bus_slot_unref(query->slot);
    }
}
TiredUnitQueryResult tired_unit_query_result(const TiredUnitQuery *query)
{
    assert(query != NULL);
    TiredUnitQueryResult result = {.done = query->done, .error = query->error};
    TiredManagerIdentityResult owner = tired_manager_identity_result(query->identity);
    if (result.error.status == TIRED_OK && owner.error.status != TIRED_OK)
    {
        result.done = true;
        result.error = owner.error;
    }
    if (result.done && result.error.status == TIRED_OK)
    {
        result.file_found = query->file_found;
        result.object_found = query->object_found;
        result.unit_name = query->name.data;
        result.file_state = query->file_state.data;
        result.object_path = query->object_path.data;
        result.observation = &query->observation;
    }
    return result;
}
void tired_unit_query_destroy(TiredUnitQuery *query)
{
    if (query == NULL)
        return;
    sd_bus_slot_unref(query->slot);
    sd_bus_unref(query->bus);
    tired_text_destroy(&query->owner);
    tired_text_destroy(&query->name);
    tired_text_destroy(&query->file_state);
    tired_text_destroy(&query->object_path);
    tired_observation_destroy(&query->observation);
    free(query);
}
