#include "tired/load_paths.h"
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
    TiredTextList paths = {0};
    TiredError error = {0};
    char oversized[4098];
    memset(oversized, 'a', sizeof(oversized));
    oversized[0] = '/';
    oversized[sizeof(oversized) - 1] = '\0';
    for (unsigned scenario = 0; scenario < 6; ++scenario)
    {
        sd_bus_message *message = NULL;
        CHECK(sd_bus_message_new_signal(bus, &message, "/fixture", "org.tired.Fixture", "Paths") >=
              0);
        CHECK(sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "as") >= 0);
        CHECK(sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "s") >= 0);
        unsigned count = scenario == 0 ? 2 : scenario == 1 ? 0 : scenario == 3 ? 257 : 1;
        for (unsigned i = 0; i < count; ++i)
        {
            const char *path = scenario == 2   ? "relative"
                               : scenario == 4 ? oversized
                               : scenario == 5 ? "/bad\npath"
                                               : "/usr/lib/systemd/system";
            CHECK(sd_bus_message_append_basic(message, 's', path) >= 0);
        }
        CHECK(sd_bus_message_close_container(message) >= 0 &&
              sd_bus_message_close_container(message) >= 0);
        CHECK(sd_bus_message_seal(message, 1, 0) >= 0 && sd_bus_message_rewind(message, true) >= 0);
        bool ok = tired_load_paths_read(message, &paths, &error);
        CHECK(ok == (scenario == 0));
        CHECK(paths.count == 2 && strcmp(paths.items[0].data, "/usr/lib/systemd/system") == 0);
        sd_bus_message_unref(message);
    }
    tired_text_list_destroy(&paths);
    sd_bus_close_unref(bus);
    CHECK(close(pair[1]) == 0);
    return 0;
}
