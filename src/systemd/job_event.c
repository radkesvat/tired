#include "tired/job_event.h"
#include "tired/name.h"
#include <assert.h>
#include <string.h>

static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "job-protocol",
                           "Manager job event has an invalid envelope or payload.", 0);
}
bool tired_job_path_id(const char *path, uint32_t *id, TiredError *error)
{
    assert(path != NULL && id != NULL);
    static const char prefix[] = "/org/freedesktop/systemd1/job/";
    size_t length = strnlen(path, 64), offset = sizeof(prefix) - 1;
    if (length <= offset || length >= 64 || memcmp(path, prefix, offset) != 0 ||
        (length > offset + 1 && path[offset] == '0'))
        return invalid(error);
    uint64_t value;
    if (!tired_parse_u64(path + offset, length - offset, 0, UINT32_MAX, &value, error))
        return false;
    *id = (uint32_t)value;
    tired_error_clear(error);
    return true;
}
bool tired_job_event_read(sd_bus_message *message, const char *owner, TiredJobEvent *output,
                          TiredError *error)
{
    assert(message != NULL && owner != NULL && output != NULL);
    const char *sender = sd_bus_message_get_sender(message),
               *object = sd_bus_message_get_path(message);
    if (owner[0] != ':' || sender == NULL || strcmp(sender, owner) != 0 || object == NULL ||
        strcmp(object, "/org/freedesktop/systemd1") != 0 ||
        sd_bus_message_is_signal(message, "org.freedesktop.systemd1.Manager", "JobRemoved") <= 0 ||
        sd_bus_message_has_signature(message, "uoss") <= 0)
        return invalid(error);
    TiredJobEvent event = {0};
    const char *path = NULL, *unit = NULL, *result = NULL;
    if (sd_bus_message_read(message, "uoss", &event.id, &path, &unit, &result) <= 0 ||
        sd_bus_message_at_end(message, true) <= 0)
        return invalid(error);
    uint32_t path_id;
    if (!tired_job_path_id(path, &path_id, error) || path_id != event.id)
        return invalid(error);
    size_t unit_length = strnlen(unit, sizeof(event.unit));
    if (unit_length <= 8 || unit_length >= sizeof(event.unit) ||
        memcmp(unit + unit_length - 8, ".service", 8) != 0 ||
        !tired_name_validate_base(unit, unit_length - 8, error))
        return invalid(error);
    size_t length = strnlen(result, sizeof(event.result));
    if (length == 0 || length >= sizeof(event.result))
        return invalid(error);
    for (size_t i = 0; i < length; ++i)
        if ((result[i] < 'a' || result[i] > 'z') && result[i] != '-')
            return invalid(error);
    static const char *const outcomes[] = {"",       "done",        "canceled",  "timeout",
                                           "failed", "dependency",  "skipped",   "invalid",
                                           "assert", "unsupported", "collected", "once"};
    for (size_t i = 1; i < sizeof(outcomes) / sizeof(outcomes[0]); ++i)
        if (strcmp(result, outcomes[i]) == 0)
            event.outcome = (TiredJobOutcome)i;
    memcpy(event.unit, unit, unit_length + 1);
    memcpy(event.result, result, length + 1);
    memcpy(event.path, path, strlen(path) + 1);
    *output = event;
    tired_error_clear(error);
    return true;
}
