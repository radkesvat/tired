#include "inspection_live.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <time.h>

typedef enum
{
    IDENTITY,
    VERSION,
    UNITS,
    CONFIGURATION
} Phase;
static bool remaining(TiredInspectionLive *live, uint64_t deadline, unsigned *milliseconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return tired_error_set(&live->error, TIRED_INTERNAL, "recovery-clock",
                               "Cannot read recovery inspection clock.", errno);
    uint64_t current = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
    if (current >= deadline)
        return tired_error_set(&live->error, TIRED_RUNTIME_FAILED, "recovery-observation-timeout",
                               "Live recovery observation timed out.", 0);
    *milliseconds = (unsigned)((deadline - current + 999) / 1000);
    return true;
}
static bool drive(TiredInspectionLive *live, Phase phase, uint64_t deadline)
{
    for (;;)
    {
        unsigned left;
        if (!remaining(live, deadline, &left))
            return false;
        bool done = phase == IDENTITY        ? tired_manager_identity_step(live->identity)
                    : phase == VERSION       ? tired_manager_probe_step(live->version)
                    : phase == CONFIGURATION ? tired_unit_query_step(live->configuration)
                                             : tired_unit_batch_step(live->batch);
        if (done)
            return true;
        struct pollfd descriptor;
        uint64_t query_deadline;
        bool ok = phase == IDENTITY  ? tired_manager_identity_poll(live->identity, &descriptor,
                                                                   &query_deadline, &live->error)
                  : phase == VERSION ? tired_manager_probe_poll(live->version, &descriptor,
                                                                &query_deadline, &live->error)
                  : phase == CONFIGURATION ? tired_unit_query_poll(live->configuration, &descriptor,
                                                                   &query_deadline, &live->error)
                                           : tired_unit_batch_poll(live->batch, &descriptor,
                                                                   &query_deadline, &live->error);
        if (!ok)
            return false;
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return tired_error_set(&live->error, TIRED_INTERNAL, "recovery-clock",
                                   "Cannot read recovery inspection clock.", errno);
        uint64_t current = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
        if (deadline < query_deadline)
            query_deadline = deadline;
        uint64_t wait = current >= query_deadline ? 0 : (query_deadline - current + 999) / 1000;
        int timeout = wait > 100 ? 100 : (int)wait;
        if (poll(&descriptor, 1, timeout) < 0 && errno != EINTR)
            return tired_error_set(&live->error, TIRED_RUNTIME_FAILED, "recovery-poll",
                                   "Cannot wait for live recovery observations.", errno);
    }
}
static void collect(bool user, const TiredTextList *names, bool configuration,
                    TiredInspectionLive *live)
{
    assert(names != NULL && live != NULL && live->bus == NULL && live->identity == NULL &&
           live->batch == NULL && live->configuration == NULL);
    if (names->count == 0)
        return;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        tired_error_set(&live->error, TIRED_INTERNAL, "recovery-clock",
                        "Cannot read recovery inspection clock.", errno);
        return;
    }
    uint64_t deadline = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000 + 5000000;
    unsigned left;
    if (!tired_manager_bus_open(user, &live->bus, &live->error) ||
        !remaining(live, deadline, &left) ||
        !tired_manager_identity_start(live->bus, user, left, &live->identity, &live->error) ||
        !drive(live, IDENTITY, deadline))
        return;
    TiredManagerIdentityResult identity = tired_manager_identity_result(live->identity);
    if (!identity.ready)
    {
        live->error = identity.error;
        return;
    }
    if (!remaining(live, deadline, &left) ||
        !tired_manager_probe_start_unique(live->bus, identity.unique_name, left, &live->version,
                                          &live->error) ||
        !drive(live, VERSION, deadline))
        return;
    live->error = tired_manager_probe_result(live->version).error;
    if (live->error.status != TIRED_OK || !remaining(live, deadline, &left))
        return;
    if (configuration)
    {
        assert(names->count == 1);
        TiredText base = {0};
        bool started =
            tired_name_explicit(names->items[0].data, names->items[0].length, &base,
                                &live->error) &&
            tired_unit_query_start_lookup(live->identity, &base, TIRED_UNIT_LOAD_CONFIGURATION,
                                          left, &live->configuration, &live->error);
        tired_text_destroy(&base);
        if (!started)
            return;
        if (!drive(live, CONFIGURATION, deadline))
        {
            tired_unit_query_cancel(live->configuration);
            return;
        }
        live->error = tired_unit_query_result(live->configuration).error;
        if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        {
            tired_error_set(&live->error, TIRED_INTERNAL, "inspection-clock",
                            "Cannot timestamp configuration observation.", errno);
            return;
        }
        live->configuration_completed_usec =
            (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
        return;
    }
    if (!tired_unit_batch_start(live->identity, names, left, &live->batch, &live->error))
        return;
    if (!drive(live, UNITS, deadline))
    {
        tired_unit_batch_cancel(live->batch);
        return;
    }
    live->error = tired_unit_batch_result(live->batch).error;
}
void tired_inspection_live_collect(bool user, const TiredTextList *names, TiredInspectionLive *live)
{
    collect(user, names, false, live);
}
void tired_inspection_configuration_collect(bool user, const TiredText *name,
                                            TiredInspectionLive *live)
{
    assert(name != NULL);
    TiredText borrowed = *name;
    TiredTextList names = {.items = &borrowed, .count = 1};
    collect(user, &names, true, live);
}
TiredUnitBatchItem tired_inspection_live_item(const TiredInspectionLive *live, size_t index)
{
    assert(live != NULL);
    TiredUnitBatchItem item = {0};
    if (live->batch != NULL)
        item = tired_unit_batch_item(live->batch, index);
    if (live->configuration != NULL)
    {
        assert(index == 0);
        item.attempted = true;
        item.query = tired_unit_query_result(live->configuration);
        item.completed_realtime_usec = live->configuration_completed_usec;
    }
    if (item.completed_realtime_usec == 0 && live->error.status != TIRED_OK)
    {
        item.query.done = true;
        item.query.error = live->error;
    }
    return item;
}
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
struct json_object *tired_inspection_live_json(const TiredUnitBatchItem *item)
{
    assert(item != NULL);
    struct json_object *object = json_object_new_object(), *properties = NULL;
    if (object == NULL)
        return NULL;
    bool known = item->query.done && item->query.error.status == TIRED_OK;
    if (!add(object, "status", json_object_new_string(known ? "observed" : "unknown")) ||
        !add(object, "attempted", json_object_new_boolean(item->attempted)) ||
        !add(object, "completed_realtime_usec",
             json_object_new_uint64(item->completed_realtime_usec)))
        goto failed;
    if (!known)
    {
        if (!add(object, "error",
                 json_object_new_string(item->query.error.code == NULL ? "incomplete"
                                                                       : item->query.error.code)) ||
            !add(object, "message",
                 json_object_new_string(item->query.error.message == NULL
                                            ? "Live state is unknown."
                                            : item->query.error.message)))
            goto failed;
        return object;
    }
    if (!add(object, "file_found", json_object_new_boolean(item->query.file_found)) ||
        !add(object, "object_found", json_object_new_boolean(item->query.object_found)))
        goto failed;
    if (item->query.file_state != NULL &&
        !add(object, "file_state", json_object_new_string(item->query.file_state)))
        goto failed;
    properties = json_object_new_object();
    if (properties == NULL)
        goto failed;
    if (item->query.observation != NULL)
        for (size_t i = 0; i < TIRED_OBS_COUNT; ++i)
        {
            const TiredObservedValue *value = &item->query.observation->fields[i];
            if (!value->known)
                continue;
            const TiredObservationField *field = tired_observation_field((TiredObservationId)i);
            struct json_object *encoded = NULL;
            if (field->type == TIRED_OBS_TEXT_LIST)
            {
                encoded = json_object_new_array();
                if (encoded == NULL)
                    goto failed;
                for (size_t j = 0; j < value->value.list.count; ++j)
                {
                    const TiredText *path = &value->value.list.items[j];
                    struct json_object *element =
                        json_object_new_string_len(path->data, (int)path->length);
                    if (element == NULL || json_object_array_add(encoded, element) != 0)
                    {
                        json_object_put(element);
                        json_object_put(encoded);
                        goto failed;
                    }
                }
            }
            else
                encoded = field->type == TIRED_OBS_TEXT
                              ? json_object_new_string_len(value->value.text.data,
                                                           (int)value->value.text.length)
                          : field->type == TIRED_OBS_BOOL
                              ? json_object_new_boolean(value->value.boolean)
                          : field->type == TIRED_OBS_I32
                              ? json_object_new_int64(value->value.signed_value)
                              : json_object_new_uint64(value->value.unsigned_value);
            if (!add(properties, field->property, encoded))
                goto failed;
        }
    bool inserted = add(object, "properties", properties);
    properties = NULL;
    if (!inserted)
        goto failed;
    return object;
failed:
    json_object_put(properties);
    json_object_put(object);
    return NULL;
}
void tired_inspection_live_destroy(TiredInspectionLive *live)
{
    if (live == NULL)
        return;
    tired_unit_query_destroy(live->configuration);
    tired_unit_batch_destroy(live->batch);
    tired_manager_probe_destroy(live->version);
    tired_manager_identity_destroy(live->identity);
    sd_bus_close_unref(live->bus);
    *live = (TiredInspectionLive){0};
}
