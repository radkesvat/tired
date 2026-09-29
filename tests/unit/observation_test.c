#include "tired/observation.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static sd_bus_message *dictionary(sd_bus *bus)
{
    sd_bus_message *message = NULL;
    if (sd_bus_message_new_signal(bus, &message, "/fixture", "org.tired.Fixture", "Properties") < 0)
        return NULL;
    if (sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}") < 0)
        return sd_bus_message_unref(message);
    return message;
}
static bool seal(sd_bus_message *message)
{
    return sd_bus_message_close_container(message) >= 0 &&
           sd_bus_message_seal(message, 1, 0) >= 0 && sd_bus_message_rewind(message, true) >= 0;
}
int main(void)
{
    sd_bus *bus = NULL;
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    CHECK(sd_bus_new(&bus) >= 0 && sd_bus_set_fd(bus, pair[0], pair[0]) >= 0 &&
          sd_bus_start(bus) >= 0);
    TiredUnitObservation observation = {0};
    TiredError error = {0};
    sd_bus_message *message = dictionary(bus);
    CHECK(message != NULL);
    CHECK(sd_bus_message_append(message, "{sv}", "Id", "s", "fixture.service") >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "LoadState", "s", "loaded") >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "FragmentPath", "s", "") >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "ActiveEnterTimestamp", "t",
                                UINT64_C(9000000000)) >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "FutureDependency", "as", 2, "one", "two") >= 0);
    CHECK(seal(message));
    CHECK(tired_observation_read(message, TIRED_OBSERVE_UNIT, &observation, &error));
    CHECK(observation.fields[TIRED_OBS_ID].known &&
          strcmp(observation.fields[TIRED_OBS_ID].value.text.data, "fixture.service") == 0);
    CHECK(!observation.fields[TIRED_OBS_ACTIVE_STATE].known);
    CHECK(observation.fields[TIRED_OBS_FRAGMENT_PATH].known &&
          observation.fields[TIRED_OBS_FRAGMENT_PATH].value.text.length == 0);
    CHECK(observation.fields[TIRED_OBS_ACTIVE_ENTER].value.unsigned_value == UINT64_C(9000000000));
    sd_bus_message_unref(message);
    message = dictionary(bus);
    CHECK(message != NULL);
    CHECK(sd_bus_message_append(message, "{sv}", "MainPID", "u", 0U) >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "ExecMainStatus", "i", 0) >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "ExecMainCode", "i", -1) >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "NRestarts", "u", UINT32_MAX) >= 0);
    CHECK(sd_bus_message_append(message, "{sv}", "Result", "s", "success") >= 0);
    CHECK(seal(message));
    CHECK(tired_observation_read(message, TIRED_OBSERVE_SERVICE, &observation, &error));
    CHECK(observation.fields[TIRED_OBS_MAIN_PID].known &&
          observation.fields[TIRED_OBS_MAIN_PID].value.unsigned_value == 0);
    CHECK(observation.fields[TIRED_OBS_EXIT_STATUS].known &&
          observation.fields[TIRED_OBS_EXIT_STATUS].value.signed_value == 0);
    CHECK(observation.fields[TIRED_OBS_EXIT_CODE].value.signed_value == -1);
    CHECK(observation.fields[TIRED_OBS_RESTARTS].value.unsigned_value == UINT32_MAX);
    CHECK(observation.fields[TIRED_OBS_ID].known);
    sd_bus_message_unref(message);
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        message = dictionary(bus);
        CHECK(message != NULL);
        if (scenario == 0)
            CHECK(sd_bus_message_append(message, "{sv}", "MainPID", "s", "0") >= 0);
        else if (scenario == 1 || scenario == 2)
        {
            const char *name = scenario == 1 ? "MainPID" : "FutureProperty";
            CHECK(sd_bus_message_append(message, "{sv}", name, "u", 1U) >= 0);
            CHECK(sd_bus_message_append(message, "{sv}", name, "u", 2U) >= 0);
        }
        else
            for (unsigned i = 0; i < 513; ++i)
            {
                char name[32];
                (void)snprintf(name, sizeof(name), "FutureProperty%u", i);
                CHECK(sd_bus_message_append(message, "{sv}", name, "u", i) >= 0);
            }
        CHECK(seal(message));
        CHECK(!tired_observation_read(message, TIRED_OBSERVE_SERVICE, &observation, &error));
        CHECK(observation.fields[TIRED_OBS_MAIN_PID].known &&
              observation.fields[TIRED_OBS_MAIN_PID].value.unsigned_value == 0);
        CHECK(strcmp(observation.fields[TIRED_OBS_RESULT].value.text.data, "success") == 0);
        sd_bus_message_unref(message);
    }
    message = dictionary(bus);
    CHECK(message != NULL && seal(message));
    CHECK(tired_observation_read(message, TIRED_OBSERVE_SERVICE, &observation, &error));
    CHECK(!observation.fields[TIRED_OBS_MAIN_PID].known &&
          !observation.fields[TIRED_OBS_RESULT].known);
    CHECK(observation.fields[TIRED_OBS_ID].known);
    sd_bus_message_unref(message);
    tired_observation_destroy(&observation);
    tired_observation_destroy(&observation);
    sd_bus_close_unref(bus);
    CHECK(close(pair[1]) == 0);
    return 0;
}
