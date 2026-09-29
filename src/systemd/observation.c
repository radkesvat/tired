#include "tired/observation.h"
#include "tired/capture.h"
#include <assert.h>
#include <string.h>

static const TiredObservationField fields[TIRED_OBS_COUNT] = {
    {"Id", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT},
    {"LoadState", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT},
    {"ActiveState", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT},
    {"SubState", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT},
    {"UnitFileState", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT},
    {"FragmentPath", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT},
    {"Result", TIRED_OBSERVE_SERVICE, TIRED_OBS_TEXT},
    {"MainPID", TIRED_OBSERVE_SERVICE, TIRED_OBS_U32},
    {"ExecMainCode", TIRED_OBSERVE_SERVICE, TIRED_OBS_I32},
    {"ExecMainStatus", TIRED_OBSERVE_SERVICE, TIRED_OBS_I32},
    {"NRestarts", TIRED_OBSERVE_SERVICE, TIRED_OBS_U32},
    {"ActiveEnterTimestamp", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"ActiveEnterTimestampMonotonic", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"ActiveExitTimestamp", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"ActiveExitTimestampMonotonic", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"InactiveEnterTimestamp", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"InactiveEnterTimestampMonotonic", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"InactiveExitTimestamp", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"InactiveExitTimestampMonotonic", TIRED_OBSERVE_UNIT, TIRED_OBS_U64},
    {"ExecMainStartTimestamp", TIRED_OBSERVE_SERVICE, TIRED_OBS_U64},
    {"ExecMainStartTimestampMonotonic", TIRED_OBSERVE_SERVICE, TIRED_OBS_U64},
    {"ExecMainExitTimestamp", TIRED_OBSERVE_SERVICE, TIRED_OBS_U64},
    {"ExecMainExitTimestampMonotonic", TIRED_OBSERVE_SERVICE, TIRED_OBS_U64},
    {"DropInPaths", TIRED_OBSERVE_UNIT, TIRED_OBS_TEXT_LIST}};
const TiredObservationField *tired_observation_field(TiredObservationId id)
{
    return (unsigned)id < TIRED_OBS_COUNT ? &fields[id] : NULL;
}
void tired_observation_destroy(TiredUnitObservation *observation)
{
    if (observation == NULL)
        return;
    for (unsigned i = 0; i < TIRED_OBS_COUNT; ++i)
        if (observation->fields[i].known && fields[i].type == TIRED_OBS_TEXT)
            tired_text_destroy(&observation->fields[i].value.text);
        else if (observation->fields[i].known && fields[i].type == TIRED_OBS_TEXT_LIST)
            tired_text_list_destroy(&observation->fields[i].value.list);
    *observation = (TiredUnitObservation){0};
}
static bool protocol(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "observation-protocol",
                           "Manager property dictionary has an invalid shape, name, or value type.",
                           0);
}
static bool read_value(sd_bus_message *message, unsigned id, TiredObservedValue *value,
                       TiredError *error)
{
    static const char *signatures[] = {"s", "u", "i", "t", "as"};
    const char *signature = signatures[fields[id].type];
    if (sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, signature) <= 0)
        return protocol(error);
    int rc;
    if (fields[id].type == TIRED_OBS_TEXT)
    {
        const char *text = NULL;
        if (sd_bus_message_read_basic(message, 's', &text) <= 0)
            return protocol(error);
        size_t limit = id == TIRED_OBS_FRAGMENT_PATH ? 4096 : 256;
        size_t length = strnlen(text, limit + 1);
        if (length > limit || !tired_validate_text(text, length, true, error))
            return protocol(error);
        if (!tired_text_set(&value->value.text, text, length, limit, error))
            return false;
        rc = 1;
    }
    else if (fields[id].type == TIRED_OBS_TEXT_LIST)
    {
        if (sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "s") <= 0)
            return protocol(error);
        value->known = true; /* Partial list owns allocations even if a later item fails. */
        for (;;)
        {
            const char *path = NULL;
            rc = sd_bus_message_read_basic(message, 's', &path);
            if (rc < 0)
                return protocol(error);
            if (rc == 0)
                break;
            size_t length = strnlen(path, 4097);
            if (length == 0 || length > 4096 || path[0] != '/' ||
                !tired_validate_text(path, length, true, error))
                return protocol(error);
            if (!tired_text_list_append(&value->value.list, path, length, 256, 256U * 1024U, error))
                return false;
        }
        if (sd_bus_message_exit_container(message) < 0)
            return protocol(error);
        rc = 1;
    }
    else if (fields[id].type == TIRED_OBS_I32)
    {
        int32_t number;
        rc = sd_bus_message_read_basic(message, 'i', &number);
        if (rc > 0)
            value->value.signed_value = number;
    }
    else if (fields[id].type == TIRED_OBS_U32)
    {
        uint32_t number;
        rc = sd_bus_message_read_basic(message, 'u', &number);
        if (rc > 0)
            value->value.unsigned_value = number;
    }
    else
        rc = sd_bus_message_read_basic(message, 't', &value->value.unsigned_value);
    /* Mark ownership before further parsing can fail. */
    value->known = true;
    if (rc <= 0 || sd_bus_message_exit_container(message) < 0)
        return protocol(error);
    return true;
}
bool tired_observation_read(sd_bus_message *message, TiredObservationInterface interface,
                            TiredUnitObservation *observation, TiredError *error)
{
    assert(message != NULL && observation != NULL);
    if ((interface != TIRED_OBSERVE_UNIT && interface != TIRED_OBSERVE_SERVICE) ||
        sd_bus_message_has_signature(message, "a{sv}") <= 0)
        return protocol(error);
    TiredUnitObservation result = {0};
    TiredTextList names = {0};
    bool ok = false;
    for (unsigned i = 0; i < TIRED_OBS_COUNT; ++i)
    {
        if (fields[i].interface == interface || !observation->fields[i].known)
            continue;
        if (fields[i].type == TIRED_OBS_TEXT)
        {
            const TiredText *text = &observation->fields[i].value.text;
            if (!tired_text_set(&result.fields[i].value.text, text->data, text->length, 4096,
                                error))
                goto done;
            result.fields[i].known = true;
        }
        else if (fields[i].type == TIRED_OBS_TEXT_LIST)
        {
            result.fields[i].known = true;
            const TiredTextList *list = &observation->fields[i].value.list;
            for (size_t j = 0; j < list->count; ++j)
                if (!tired_text_list_append(&result.fields[i].value.list, list->items[j].data,
                                            list->items[j].length, 256, 256U * 1024U, error))
                    goto done;
        }
        else
            result.fields[i] = observation->fields[i];
    }
    if (sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}") <= 0)
    {
        protocol(error);
        goto done;
    }
    for (;;)
    {
        int rc = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
        if (rc < 0)
        {
            protocol(error);
            goto done;
        }
        if (rc == 0)
            break;
        const char *name = NULL;
        if (sd_bus_message_read_basic(message, 's', &name) <= 0 || strnlen(name, 256) > 255 ||
            sd_bus_member_name_is_valid(name) <= 0)
        {
            protocol(error);
            goto done;
        }
        for (size_t i = 0; i < names.count; ++i)
            if (strcmp(name, names.items[i].data) == 0)
            {
                tired_error_set(error, TIRED_INVALID, "observation-duplicate",
                                "Manager returned a duplicate property.", 0);
                goto done;
            }
        if (!tired_text_list_append(&names, name, strlen(name), 512, 128U * 1024U, error))
            goto done;
        unsigned id = TIRED_OBS_COUNT;
        for (unsigned i = 0; i < TIRED_OBS_COUNT; ++i)
            if (strcmp(fields[i].property, name) == 0)
            {
                id = i;
                break;
            }
        if (id == TIRED_OBS_COUNT)
        {
            if (sd_bus_message_skip(message, "v") <= 0)
            {
                protocol(error);
                goto done;
            }
        }
        else if (fields[id].interface != interface)
        {
            protocol(error);
            goto done;
        }
        else if (!read_value(message, id, &result.fields[id], error))
            goto done;
        if (sd_bus_message_exit_container(message) < 0)
        {
            protocol(error);
            goto done;
        }
    }
    if (sd_bus_message_exit_container(message) < 0 || sd_bus_message_at_end(message, true) <= 0)
    {
        protocol(error);
        goto done;
    }
    tired_observation_destroy(observation);
    *observation = result;
    result = (TiredUnitObservation){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_observation_destroy(&result);
    tired_text_list_destroy(&names);
    return ok;
}
