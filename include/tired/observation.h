#ifndef TIRED_OBSERVATION_H
#define TIRED_OBSERVATION_H
#include "tired/value.h"
#include <systemd/sd-bus.h>
typedef enum
{
    TIRED_OBSERVE_UNIT,
    TIRED_OBSERVE_SERVICE
} TiredObservationInterface;
typedef enum
{
    TIRED_OBS_TEXT,
    TIRED_OBS_U32,
    TIRED_OBS_I32,
    TIRED_OBS_U64,
    TIRED_OBS_TEXT_LIST,
    TIRED_OBS_BOOL
} TiredObservationType;
typedef enum
{
    TIRED_OBS_ID,
    TIRED_OBS_LOAD_STATE,
    TIRED_OBS_ACTIVE_STATE,
    TIRED_OBS_SUB_STATE,
    TIRED_OBS_FILE_STATE,
    TIRED_OBS_FRAGMENT_PATH,
    TIRED_OBS_RESULT,
    TIRED_OBS_MAIN_PID,
    TIRED_OBS_EXIT_CODE,
    TIRED_OBS_EXIT_STATUS,
    TIRED_OBS_RESTARTS,
    TIRED_OBS_ACTIVE_ENTER,
    TIRED_OBS_ACTIVE_ENTER_MONOTONIC,
    TIRED_OBS_ACTIVE_EXIT,
    TIRED_OBS_ACTIVE_EXIT_MONOTONIC,
    TIRED_OBS_INACTIVE_ENTER,
    TIRED_OBS_INACTIVE_ENTER_MONOTONIC,
    TIRED_OBS_INACTIVE_EXIT,
    TIRED_OBS_INACTIVE_EXIT_MONOTONIC,
    TIRED_OBS_EXEC_START,
    TIRED_OBS_EXEC_START_MONOTONIC,
    TIRED_OBS_EXEC_EXIT,
    TIRED_OBS_EXEC_EXIT_MONOTONIC,
    TIRED_OBS_DROP_IN_PATHS,
    TIRED_OBS_NEED_DAEMON_RELOAD,
    TIRED_OBS_COUNT
} TiredObservationId;
typedef struct
{
    const char *property;
    TiredObservationInterface interface;
    TiredObservationType type;
} TiredObservationField;
typedef struct
{
    bool known;
    union
    {
        TiredText text;
        TiredTextList list;
        uint64_t unsigned_value;
        int64_t signed_value;
        bool boolean;
    } value;
} TiredObservedValue;
typedef struct
{
    TiredObservedValue fields[TIRED_OBS_COUNT];
} TiredUnitObservation;
const TiredObservationField *tired_observation_field(TiredObservationId id);
void tired_observation_destroy(TiredUnitObservation *observation);
/* Decode an unread GetAll a{sv} body. Output starts zeroed; failure preserves it.
 * Replaces only the selected interface, clearing absent fields to unknown. Other
 * interface values are copied. Unknown property names are skipped, duplicates and
 * incorrect types rejected. This is decoding, not sender/identity verification. */
bool tired_observation_read(sd_bus_message *message, TiredObservationInterface interface,
                            TiredUnitObservation *observation, TiredError *error);
#endif
