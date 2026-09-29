#include "tired/job_event.h"
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
int main(void)
{
    sd_bus *bus = NULL;
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    CHECK(sd_bus_new(&bus) >= 0 && sd_bus_set_fd(bus, pair[0], pair[0]) >= 0 &&
          sd_bus_start(bus) >= 0);
    TiredError error = {0};
    uint32_t id = 99;
    CHECK(tired_job_path_id("/org/freedesktop/systemd1/job/4294967295", &id, &error) &&
          id == UINT32_MAX);
    CHECK(!tired_job_path_id("/org/freedesktop/systemd1/job/4294967296", &id, &error) &&
          id == UINT32_MAX);
    CHECK(!tired_job_path_id("/org/freedesktop/systemd1/job/01", &id, &error));
    CHECK(!tired_job_path_id("/org/freedesktop/systemd1/job/+1", &id, &error));
    CHECK(!tired_job_path_id("/other/1", &id, &error));
    const char *outcomes[] = {"done",        "canceled",  "timeout", "failed",
                              "dependency",  "skipped",   "invalid", "assert",
                              "unsupported", "collected", "once",    "future-result"};
    TiredJobEvent event = {0};
    for (unsigned scenario = 0; scenario < 20; ++scenario)
    {
        sd_bus_message *message = NULL;
        CHECK(sd_bus_message_new_signal(bus, &message,
                                        scenario == 17 ? "/wrong" : "/org/freedesktop/systemd1",
                                        "org.freedesktop.systemd1.Manager",
                                        scenario == 18 ? "JobNew" : "JobRemoved") >= 0);
        CHECK(sd_bus_message_set_sender(message, scenario == 12 ? ":1.43" : ":1.42") >= 0);
        const char *result = scenario < 12    ? outcomes[scenario]
                             : scenario == 15 ? "done\n"
                                              : "done";
        CHECK(sd_bus_message_append(
                  message, "uoss", scenario == 13 ? 2U : 1U, "/org/freedesktop/systemd1/job/1",
                  scenario == 14 ? "../bad.service" : "fixture.service", result) >= 0);
        if (scenario == 16)
            CHECK(sd_bus_message_append(message, "s", "extra") >= 0);
        CHECK(sd_bus_message_seal(message, 1, 0) >= 0 && sd_bus_message_rewind(message, true) >= 0);
        bool ok = tired_job_event_read(
            message, scenario == 19 ? "org.freedesktop.systemd1" : ":1.42", &event, &error);
        CHECK(ok == (scenario < 12));
        if (ok)
        {
            CHECK(event.id == 1 && strcmp(event.unit, "fixture.service") == 0);
            CHECK(strcmp(event.result, outcomes[scenario]) == 0);
            CHECK(event.outcome ==
                  (scenario == 11 ? TIRED_JOB_UNKNOWN : (TiredJobOutcome)(scenario + 1)));
        }
        else
            CHECK(event.outcome == TIRED_JOB_UNKNOWN && strcmp(event.result, "future-result") == 0);
        sd_bus_message_unref(message);
    }
    sd_bus_close_unref(bus);
    CHECK(close(pair[1]) == 0);
    return 0;
}
