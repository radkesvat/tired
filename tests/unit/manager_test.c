#include "tired/manager.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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
typedef struct
{
    const char *version, *error;
    bool wrong, silent;
} Reply;
static int respond(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)error;
    Reply *reply = userdata;
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get") <= 0)
        return 0;
    const char *interface = NULL, *property = NULL;
    if (sd_bus_message_read(message, "ss", &interface, &property) <= 0 ||
        strcmp(interface, "org.freedesktop.systemd1.Manager") != 0 ||
        strcmp(property, "Version") != 0 || sd_bus_message_get_auto_start(message) != 0 ||
        sd_bus_message_get_allow_interactive_authorization(message) != 0)
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                          "Invalid fixture request");
    if (reply->silent)
        return 1;
    if (reply->error != NULL)
        return sd_bus_reply_method_errorf(message, reply->error, "Fixture error");
    return reply->wrong ? sd_bus_reply_method_return(message, "s", reply->version)
                        : sd_bus_reply_method_return(message, "v", "s", reply->version);
}
static bool finish(TiredManagerProbe *probe, sd_bus *server)
{
    for (unsigned i = 0; i < 2000; ++i)
    {
        if (sd_bus_process(server, NULL) < 0)
            return false;
        if (tired_manager_probe_step(probe))
            return true;
        struct timespec delay = {.tv_nsec = 1000000};
        (void)nanosleep(&delay, NULL);
    }
    return false;
}
int main(void)
{
    int pair[2];
    sd_bus *client = NULL, *server = NULL;
    sd_bus_slot *slot = NULL;
    sd_id128_t id;
    TiredError error = {0};
    TiredManagerProbe *probe = NULL;
    Reply reply = {.version = "249.11-0ubuntu3.22"};
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    CHECK(sd_bus_new(&client) >= 0 && sd_bus_new(&server) >= 0);
    CHECK(sd_id128_randomize(&id) >= 0);
    CHECK(sd_bus_set_fd(client, pair[0], pair[0]) >= 0);
    CHECK(sd_bus_set_fd(server, pair[1], pair[1]) >= 0);
    CHECK(sd_bus_set_server(server, 1, id) >= 0);
    CHECK(sd_bus_add_filter(server, &slot, respond, &reply) >= 0);
    CHECK(sd_bus_start(server) >= 0 && sd_bus_start(client) >= 0);
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    struct pollfd descriptor;
    uint64_t deadline;
    CHECK(tired_manager_probe_poll(probe, &descriptor, &deadline, &error));
    CHECK(descriptor.fd >= 0 && deadline != UINT64_MAX);
    CHECK(finish(probe, server));
    TiredManagerProbeResult result = tired_manager_probe_result(probe);
    CHECK(result.done && result.error.status == TIRED_OK && result.major_version == 249);
    CHECK(strcmp(result.version, reply.version) == 0);
    tired_manager_probe_destroy(probe);
    probe = NULL;
    const char *versions[] = {"248", "unknown", "249evil", "999999999999999999999"};
    for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i)
    {
        reply.version = versions[i];
        CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
        CHECK(finish(probe, server));
        CHECK(tired_manager_probe_result(probe).error.status == TIRED_UNSUPPORTED);
        tired_manager_probe_destroy(probe);
        probe = NULL;
    }
    reply = (Reply){.version = "249", .wrong = true};
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    CHECK(finish(probe, server));
    CHECK(tired_manager_probe_result(probe).error.status == TIRED_INVALID);
    tired_manager_probe_destroy(probe);
    probe = NULL;
    char oversized[258];
    memset(oversized, '1', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    reply = (Reply){.version = oversized};
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    CHECK(finish(probe, server));
    CHECK(tired_manager_probe_result(probe).error.status == TIRED_INVALID);
    tired_manager_probe_destroy(probe);
    probe = NULL;
    const char *errors[] = {SD_BUS_ERROR_ACCESS_DENIED, SD_BUS_ERROR_SERVICE_UNKNOWN,
                            SD_BUS_ERROR_UNKNOWN_PROPERTY};
    const TiredStatus statuses[] = {TIRED_AUTHORIZATION, TIRED_NOT_FOUND, TIRED_UNSUPPORTED};
    for (size_t i = 0; i < 3; ++i)
    {
        reply = (Reply){.error = errors[i]};
        CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
        CHECK(finish(probe, server));
        result = tired_manager_probe_result(probe);
        CHECK(result.error.status == statuses[i] &&
              strcmp(result.remote_error_name, errors[i]) == 0);
        tired_manager_probe_destroy(probe);
        probe = NULL;
    }
    reply = (Reply){.silent = true};
    CHECK(tired_manager_probe_start(client, 20, &probe, &error));
    CHECK(finish(probe, server));
    CHECK(strcmp(tired_manager_probe_result(probe).error.code, "manager-timeout") == 0);
    tired_manager_probe_destroy(probe);
    probe = NULL;
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    tired_manager_probe_cancel(probe);
    CHECK(tired_manager_probe_step(probe));
    CHECK(tired_manager_probe_result(probe).error.status == TIRED_CANCELLED);
    tired_manager_probe_destroy(probe);
    probe = NULL;
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    tired_manager_probe_destroy(probe); /* Slot is detached even when still pending. */
    probe = NULL;
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    sd_bus_close(server);
    for (unsigned i = 0; i < 2000 && !tired_manager_probe_step(probe); ++i)
    {
        struct timespec delay = {.tv_nsec = 1000000};
        (void)nanosleep(&delay, NULL);
    }
    result = tired_manager_probe_result(probe);
    CHECK(result.done && result.error.status == TIRED_RUNTIME_FAILED);
    CHECK(strcmp(result.error.code, "manager-disconnected") == 0);
    tired_manager_probe_destroy(probe);
    sd_bus_slot_unref(slot);
    sd_bus_close_unref(server);
    sd_bus_close_unref(client);
    return 0;
}
