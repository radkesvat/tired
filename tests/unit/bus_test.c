#include "tired/io.h"
#include "tired/manager.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static int respond(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "Hello") > 0)
        return sd_bus_reply_method_return(message, "s", ":1.99");
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get") > 0)
        return sd_bus_reply_method_return(message, "v", "s", "249.11");
    return 0;
}
int main(void)
{
    int result = 1, parent = -1, listener = -1, accepted = -1;
    char fixture[] = "bus-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText directory = {0}, socket_path = {0}, link_path = {0};
    TiredError error = {0};
    sd_bus *client = NULL, *server = NULL;
    sd_bus_slot *slot = NULL;
    TiredManagerProbe *probe = NULL;
    CHECK(cwd != NULL);
    CHECK(unsetenv("XDG_RUNTIME_DIR") == 0);
    CHECK(!tired_manager_bus_open(true, &client, &error));
    CHECK(error.status == TIRED_NOT_FOUND && client == NULL);
    created = mkdtemp(fixture);
    CHECK(created != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &directory, &error));
    CHECK(tired_path_absolute(&directory, "bus", 3, &socket_path, &error));
    CHECK(tired_path_absolute(&directory, "link", 4, &link_path, &error));
    parent = open(directory.data, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(parent >= 0);
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(listener >= 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    CHECK(snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/bus", parent) > 0);
    CHECK(bind(listener, (const struct sockaddr *)&address, sizeof(address)) == 0);
    CHECK(listen(listener, 8) == 0);
    CHECK(chmod(directory.data, 0755) == 0);
    CHECK(!tired_manager_bus_connect_directory(directory.data, "bus", true, &client, &error));
    CHECK(strcmp(error.code, "bus-directory-trust") == 0);
    CHECK(chmod(directory.data, 0700) == 0);
    CHECK(symlink("bus", link_path.data) == 0);
    CHECK(!tired_manager_bus_connect_directory(directory.data, "link", true, &client, &error));
    CHECK(strcmp(error.code, "bus-socket-trust") == 0);
    CHECK(!tired_manager_bus_connect_directory(directory.data, "../bus", true, &client, &error));
    CHECK(!tired_manager_bus_connect_directory(directory.data, "missing", true, &client, &error));
    CHECK(error.status == TIRED_NOT_FOUND);
    if (getuid() == 0)
    {
        CHECK(chown(socket_path.data, 65534, (gid_t)-1) == 0);
        CHECK(!tired_manager_bus_connect_directory(directory.data, "bus", true, &client, &error));
        CHECK(strcmp(error.code, "bus-socket-trust") == 0);
        CHECK(chown(socket_path.data, 0, (gid_t)-1) == 0);
    }
    CHECK(setenv("XDG_RUNTIME_DIR", directory.data, 1) == 0);
    CHECK(setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/no/such/tired-bus", 1) == 0);
    CHECK(tired_manager_bus_open(true, &client, &error));
    accepted = accept(listener, NULL, NULL);
    CHECK(accepted >= 0);
    CHECK(sd_bus_new(&server) >= 0);
    sd_id128_t id;
    CHECK(sd_id128_randomize(&id) >= 0);
    CHECK(sd_bus_set_fd(server, accepted, accepted) >= 0);
    accepted = -1;
    CHECK(sd_bus_set_server(server, 1, id) >= 0);
    CHECK(sd_bus_add_filter(server, &slot, respond, NULL) >= 0);
    CHECK(sd_bus_start(server) >= 0);
    CHECK(tired_manager_probe_start(client, 1000, &probe, &error));
    for (unsigned i = 0; i < 2000; ++i)
    {
        CHECK(sd_bus_process(server, NULL) >= 0);
        if (tired_manager_probe_step(probe))
            break;
        struct timespec delay = {.tv_nsec = 1000000};
        (void)nanosleep(&delay, NULL);
    }
    TiredManagerProbeResult observed = tired_manager_probe_result(probe);
    CHECK(observed.done && observed.error.status == TIRED_OK && observed.major_version == 249);
    CHECK(sd_bus_is_ready(client) > 0);
    if (getuid() == 0)
    {
        /* A root-owned socket inode alone does not authenticate its listener. */
        int foreign = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0), ready[2];
        CHECK(foreign >= 0);
        struct sockaddr_un other = {.sun_family = AF_UNIX};
        CHECK(snprintf(other.sun_path, sizeof(other.sun_path), "/proc/self/fd/%d/foreign", parent) >
              0);
        CHECK(bind(foreign, (const struct sockaddr *)&other, sizeof(other)) == 0);
        CHECK(pipe(ready) == 0);
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0)
        {
            (void)close(ready[0]);
            char status = setuid(65534) == 0 && listen(foreign, 1) == 0 ? 'R' : 'F';
            (void)write(ready[1], &status, 1);
            (void)close(ready[1]);
            for (;;)
                pause();
        }
        (void)close(ready[1]);
        char status = 0;
        ssize_t count = read(ready[0], &status, 1);
        (void)close(ready[0]);
        sd_bus *unexpected = NULL;
        bool connected = count == 1 && status == 'R' &&
                         tired_manager_bus_connect_directory(directory.data, "foreign", true,
                                                             &unexpected, &error);
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        (void)close(foreign);
        (void)unlinkat(parent, "foreign", 0);
        sd_bus_close_unref(unexpected);
        CHECK(count == 1 && status == 'R' && !connected);
        CHECK(strcmp(error.code, "bus-peer") == 0);
    }
    result = 0;
cleanup:
    tired_manager_probe_destroy(probe);
    sd_bus_slot_unref(slot);
    sd_bus_close_unref(server);
    sd_bus_close_unref(client);
    if (accepted >= 0)
        (void)close(accepted);
    if (listener >= 0)
        (void)close(listener);
    if (parent >= 0)
        (void)close(parent);
    if (link_path.data != NULL)
        (void)unlink(link_path.data);
    if (socket_path.data != NULL)
        (void)unlink(socket_path.data);
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    tired_text_destroy(&directory);
    tired_text_destroy(&socket_path);
    tired_text_destroy(&link_path);
    return result;
}
