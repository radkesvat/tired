#include "tired/io.h"
#include "tired/load_paths.h"
#include "tired/manager.h"
#include "tired/manager_identity.h"
#include "tired/name_query.h"
#include "tired/unit_query.h"
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
typedef struct
{
    uint32_t uid;
    unsigned unit_case;
    unsigned path_mode;
    unsigned name_mode, name_requests;
    const char *name_directory;
    bool matched, change_during_uid, silent_match, pinned_version;
} Broker;
static int properties(sd_bus_message *request, Broker *broker)
{
    const char *interface = NULL;
    if (sd_bus_message_read(request, "s", &interface) <= 0)
        return -1;
    bool service = strcmp(interface, "org.freedesktop.systemd1.Service") == 0;
    if (service && broker->unit_case == 8)
    {
        struct timespec delay = {.tv_nsec = 30000000};
        (void)nanosleep(&delay, NULL);
    }
    if (service && broker->unit_case == 4)
        return sd_bus_reply_method_errorf(request, SD_BUS_ERROR_UNKNOWN_OBJECT, "Gone");
    sd_bus_message *message = NULL;
    int rc = sd_bus_message_new_method_return(request, &message);
    if (rc >= 0)
        rc = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}");
    if (rc >= 0 && service)
        rc = broker->unit_case == 3 ? sd_bus_message_append(message, "{sv}", "MainPID", "s", "42")
                                    : sd_bus_message_append(message, "{sv}", "MainPID", "u", 42U);
    if (rc >= 0 && !service)
        rc = sd_bus_message_append(message, "{sv}", "Id", "s", "fixture.service");
    if (rc >= 0 && !service)
        rc = sd_bus_message_append(message, "{sv}", "ActiveState", "s", "active");
    if (rc >= 0)
        rc = sd_bus_message_close_container(message);
    if (rc >= 0)
        rc = sd_bus_send(sd_bus_message_get_bus(request), message, NULL);
    sd_bus_message_unref(message);
    return rc;
}
static int changed(sd_bus *bus)
{
    sd_bus_message *message = NULL;
    int rc = sd_bus_message_new_signal(bus, &message, "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus", "NameOwnerChanged");
    if (rc >= 0)
        rc = sd_bus_message_set_sender(message, "org.freedesktop.DBus");
    if (rc >= 0)
        rc = sd_bus_message_append(message, "sss", "org.freedesktop.systemd1", ":1.42", ":1.43");
    if (rc >= 0)
        rc = sd_bus_send(bus, message, NULL);
    sd_bus_message_unref(message);
    return rc;
}
static int respond(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    Broker *broker = userdata;
    (void)error;
    bool file_query = sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                    "GetUnitFileState") > 0;
    bool object_query =
        sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager", "GetUnit") > 0;
    bool property_query =
        sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "GetAll") > 0;
    if (file_query || object_query || property_query)
    {
        const char *destination = sd_bus_message_get_destination(message);
        if (destination == NULL || strcmp(destination, ":1.42") != 0 ||
            sd_bus_message_get_auto_start(message) != 0 ||
            sd_bus_message_get_allow_interactive_authorization(message) != 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Invalid unit query");
        if (property_query)
            return properties(message, broker);
        const char *name = NULL;
        if (sd_bus_message_read(message, "s", &name) <= 0)
            return -1;
        if (broker->name_mode != 0)
        {
            ++broker->name_requests;
            if (!file_query)
                return sd_bus_reply_method_errorf(message, "org.freedesktop.systemd1.NoSuchUnit",
                                                  "Absent");
            if (broker->name_mode == 2 || strcmp(name, "fixture.service") == 0)
                return sd_bus_reply_method_return(message, "s", "disabled");
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FILE_NOT_FOUND, "Absent");
        }
        if (strcmp(name, "fixture.service") != 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Invalid unit name");
        if (file_query)
        {
            if (broker->unit_case == 1)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FILE_NOT_FOUND, "Absent");
            if (broker->unit_case == 5)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
            if (broker->unit_case == 6)
                return 1;
            return sd_bus_reply_method_return(message, "s",
                                              broker->unit_case == 2 ? "disabled" : "enabled");
        }
        if (broker->unit_case == 1 || broker->unit_case == 2)
            return sd_bus_reply_method_errorf(message, "org.freedesktop.systemd1.NoSuchUnit",
                                              "Absent");
        return sd_bus_reply_method_return(message, "o",
                                          "/org/freedesktop/systemd1/unit/fixture_2eservice");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "Hello") > 0)
        return sd_bus_reply_method_return(message, "s", ":1.99");
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "AddMatch") > 0)
    {
        broker->matched = true;
        return broker->silent_match ? 1 : sd_bus_reply_method_return(message, "");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "RemoveMatch") > 0)
        return sd_bus_reply_method_return(message, "");
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "GetNameOwner") > 0)
        return broker->matched
                   ? sd_bus_reply_method_return(message, "s", ":1.42")
                   : sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED, "Match missing");
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "GetConnectionUnixUser") > 0)
    {
        if (broker->change_during_uid && changed(sd_bus_message_get_bus(message)) < 0)
            return -1;
        return sd_bus_reply_method_return(message, "u", broker->uid);
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get") > 0)
    {
        const char *destination = sd_bus_message_get_destination(message);
        const char *interface = NULL, *property = NULL;
        if (sd_bus_message_read(message, "ss", &interface, &property) <= 0)
            return -1;
        if (strcmp(property, "UnitPath") == 0)
        {
            if (destination == NULL || strcmp(destination, ":1.42") != 0 ||
                strcmp(interface, "org.freedesktop.systemd1.Manager") != 0 ||
                sd_bus_message_get_auto_start(message) != 0 ||
                sd_bus_message_get_allow_interactive_authorization(message) != 0)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                                  "Invalid path query");
            if (broker->path_mode == 2)
                return 1;
            if (broker->name_directory != NULL && broker->path_mode == 0)
                return sd_bus_reply_method_return(message, "v", "as", 1, broker->name_directory);
            return broker->path_mode == 1
                       ? sd_bus_reply_method_return(message, "v", "s", "bad")
                       : sd_bus_reply_method_return(message, "v", "as", 2, "/etc/systemd/system",
                                                    "/usr/lib/systemd/system");
        }
        broker->pinned_version = destination != NULL && strcmp(destination, ":1.42") == 0;
        return sd_bus_reply_method_return(message, "v", "s", "249.11");
    }
    return 0;
}
int main(void)
{
    int result = 1, parent = -1, listener = -1, accepted = -1;
    char fixture[] = "bus-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText directory = {0}, socket_path = {0}, link_path = {0}, load_directory = {0},
              user_units = {0};
    TiredError error = {0};
    sd_bus *client = NULL, *server = NULL;
    sd_bus_slot *slot = NULL;
    TiredManagerProbe *probe = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredUnitQuery *query = NULL;
    TiredLoadPaths *paths = NULL;
    TiredNameQuery *names = NULL;
    TiredNameQuery *discovery = NULL;
    Broker broker = {.uid = (uint32_t)getuid()};
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
    CHECK(tired_path_absolute(&directory, "loads", 5, &load_directory, &error));
    CHECK(tired_path_absolute(&directory, "systemd/user", 12, &user_units, &error));
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
    CHECK(sd_bus_add_filter(server, &slot, respond, &broker) >= 0);
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
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        broker.uid = (uint32_t)getuid() ^ (scenario == 1 ? 1U : 0U);
        broker.change_during_uid = scenario == 2;
        broker.silent_match = scenario == 3;
        broker.matched = false;
        CHECK(tired_manager_identity_start(client, true, scenario == 3 ? 20 : 1000, &identity,
                                           &error));
        struct pollfd poll_descriptor;
        uint64_t poll_deadline;
        CHECK(tired_manager_identity_poll(identity, &poll_descriptor, &poll_deadline, &error));
        CHECK(poll_descriptor.fd >= 0 && poll_deadline != UINT64_MAX);
        for (unsigned i = 0; i < 2000; ++i)
        {
            CHECK(sd_bus_process(server, NULL) >= 0);
            if (tired_manager_identity_step(identity))
                break;
            struct timespec delay = {.tv_nsec = 1000000};
            (void)nanosleep(&delay, NULL);
        }
        TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
        if (scenario == 0)
        {
            CHECK(owner.ready && owner.error.status == TIRED_OK && owner.uid == getuid());
            CHECK(owner.user_scope);
            CHECK(strcmp(owner.unique_name, ":1.42") == 0);
            tired_manager_probe_destroy(probe);
            probe = NULL;
            CHECK(
                tired_manager_probe_start_unique(client, owner.unique_name, 1000, &probe, &error));
            for (unsigned i = 0; i < 2000; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                (void)tired_manager_identity_step(identity);
                if (tired_manager_probe_step(probe))
                    break;
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            CHECK(tired_manager_probe_result(probe).done &&
                  tired_manager_probe_result(probe).error.status == TIRED_OK);
            CHECK(broker.pinned_version);
            for (unsigned path_case = 0; path_case < 4; ++path_case)
            {
                broker.path_mode = path_case == 0 ? 1 : path_case == 2 ? 2 : 0;
                CHECK(tired_load_paths_start(identity, path_case == 2 ? 20 : 1000, &paths, &error));
                if (path_case == 1)
                    tired_load_paths_cancel(paths);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_load_paths_step(paths))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredLoadPathsResult discovered = tired_load_paths_result(paths);
                CHECK(discovered.done);
                if (path_case == 3)
                {
                    CHECK(discovered.error.status == TIRED_OK &&
                          discovered.directories->count == 2);
                    CHECK(strcmp(discovered.directories->items[0].data, "/etc/systemd/system") ==
                          0);
                }
                else
                {
                    const TiredStatus expected[] = {TIRED_INVALID, TIRED_CANCELLED,
                                                    TIRED_RUNTIME_FAILED};
                    CHECK(discovered.error.status == expected[path_case] &&
                          discovered.directories == NULL);
                    tired_load_paths_destroy(paths);
                    paths = NULL;
                }
            }
            TiredText unit_base = {.data = "fixture", .length = 7};
            for (unsigned unit_case = 0; unit_case < 9; ++unit_case)
            {
                broker.unit_case = unit_case;
                CHECK(tired_unit_query_start(identity, &unit_base,
                                             unit_case == 6 || unit_case == 8 ? 20 : 1000, &query,
                                             &error));
                if (unit_case == 7)
                    tired_unit_query_cancel(query);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_unit_query_step(query))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredUnitQueryResult observation = tired_unit_query_result(query);
                CHECK(observation.done);
                if (unit_case < 3)
                {
                    CHECK(observation.error.status == TIRED_OK && observation.observation != NULL);
                    CHECK(observation.file_found == (unit_case != 1) &&
                          observation.object_found == (unit_case == 0));
                    CHECK(observation.observation->fields[TIRED_OBS_MAIN_PID].known ==
                          (unit_case == 0));
                    if (unit_case == 0)
                        CHECK(observation.observation->fields[TIRED_OBS_MAIN_PID]
                                  .value.unsigned_value == 42);
                }
                else
                {
                    const TiredStatus expected[] = {TIRED_INVALID,       TIRED_CONFLICT,
                                                    TIRED_AUTHORIZATION, TIRED_RUNTIME_FAILED,
                                                    TIRED_CANCELLED,     TIRED_RUNTIME_FAILED};
                    CHECK(observation.error.status == expected[unit_case - 3] &&
                          observation.observation == NULL);
                }
                tired_unit_query_destroy(query);
                query = NULL;
            }
            broker.unit_case = 0;
            CHECK(tired_unit_query_start(identity, &unit_base, 1000, &query, &error));
            for (unsigned i = 0; i < 2000; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                if (tired_unit_query_step(query))
                    break;
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            CHECK(tired_unit_query_result(query).done &&
                  tired_unit_query_result(query).error.status == TIRED_OK);
            CHECK(symlinkat("missing", parent, "fixture-3.service") == 0);
            TiredText pending_name = {.data = "fixture-2.service", .length = 17};
            TiredTextList pending_names = {.items = &pending_name, .count = 1};
            broker.name_directory = directory.data;
            for (unsigned name_case = 0; name_case < 6; ++name_case)
            {
                broker.name_directory = name_case == 4 ? load_directory.data : directory.data;
                broker.name_mode = name_case == 2 ? 2 : 1;
                broker.name_requests = 0;
                broker.path_mode = name_case == 3 ? 1 : 0;
                CHECK(tired_name_query_start(identity, &unit_base, name_case == 1, &directory,
                                             &pending_names, name_case == 2 ? 80 : 1000, &names,
                                             &error));
                CHECK(tired_name_query_poll(names, &poll_descriptor, &poll_deadline, &error));
                CHECK(poll_descriptor.fd >= 0 && poll_deadline != UINT64_MAX);
                if (name_case == 0)
                    tired_name_query_cancel(names);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_name_query_step(names))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredNameQueryResult selected = tired_name_query_result(names);
                CHECK(selected.done);
                if (name_case == 5)
                {
                    CHECK(selected.error.status == TIRED_OK && selected.unit_name != NULL);
                    CHECK(strcmp(selected.unit_name->data, "fixture-4.service") == 0);
                    CHECK(broker.name_requests == 8);
                }
                else
                {
                    const TiredStatus expected[] = {TIRED_CANCELLED, TIRED_CONFLICT,
                                                    TIRED_RUNTIME_FAILED, TIRED_INVALID,
                                                    TIRED_CONFLICT};
                    CHECK(selected.error.status == expected[name_case] &&
                          selected.unit_name == NULL);
                    if (name_case == 2)
                        CHECK(broker.name_requests > 4);
                    if (name_case == 4)
                        CHECK(broker.name_requests == 0 &&
                              strcmp(selected.error.code, "user-unit-path-mismatch") == 0);
                    tired_name_query_destroy(names);
                    names = NULL;
                }
            }
            CHECK(setenv("XDG_CONFIG_HOME", directory.data, 1) == 0);
            CHECK(setenv("XDG_STATE_HOME", load_directory.data, 1) == 0);
            broker.name_directory = user_units.data;
            CHECK(tired_name_query_discover(identity, &unit_base, false, 1000, &discovery, &error));
            for (unsigned i = 0; i < 2000; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                if (tired_name_query_step(discovery))
                    break;
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            TiredNameQueryResult discovered_name = tired_name_query_result(discovery);
            CHECK(discovered_name.done && discovered_name.error.status == TIRED_OK);
            CHECK(strcmp(discovered_name.unit_name->data, "fixture-2.service") == 0);
            tired_name_query_destroy(discovery);
            discovery = NULL;
            CHECK(setenv("XDG_STATE_HOME", "relative", 1) == 0);
            broker.name_requests = 0;
            CHECK(
                !tired_name_query_discover(identity, &unit_base, false, 1000, &discovery, &error));
            CHECK(discovery == NULL && broker.name_requests == 0);
            broker.name_mode = 0;
            broker.name_directory = NULL;
            CHECK(unlinkat(parent, "fixture-3.service", 0) == 0);
            CHECK(changed(server) >= 0);
            for (unsigned i = 0; i < 2000; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                (void)tired_manager_identity_step(identity);
                if (tired_manager_identity_result(identity).changed)
                    break;
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            owner = tired_manager_identity_result(identity);
            CHECK(!owner.ready && owner.changed && owner.error.status == TIRED_CONFLICT);
            CHECK(tired_name_query_result(names).error.status == TIRED_CONFLICT &&
                  tired_name_query_result(names).unit_name == NULL);
            tired_name_query_destroy(names);
            names = NULL;
            CHECK(tired_load_paths_result(paths).directories == NULL &&
                  tired_load_paths_result(paths).error.status == TIRED_CONFLICT);
            tired_load_paths_destroy(paths);
            paths = NULL;
            CHECK(tired_unit_query_result(query).error.status == TIRED_CONFLICT &&
                  tired_unit_query_result(query).observation == NULL);
            tired_unit_query_destroy(query);
            query = NULL;
        }
        else if (scenario == 1)
            CHECK(!owner.ready && owner.error.status == TIRED_AUTHORIZATION);
        else if (scenario == 2)
            CHECK(!owner.ready && owner.changed && owner.error.status == TIRED_CONFLICT);
        else
            CHECK(!owner.ready && strcmp(owner.error.code, "manager-owner-timeout") == 0);
        tired_manager_identity_destroy(identity);
        identity = NULL;
    }
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
    tired_name_query_destroy(discovery);
    tired_name_query_destroy(names);
    tired_load_paths_destroy(paths);
    tired_unit_query_destroy(query);
    tired_manager_identity_destroy(identity);
    tired_manager_probe_destroy(probe);
    sd_bus_slot_unref(slot);
    sd_bus_close_unref(server);
    sd_bus_close_unref(client);
    if (accepted >= 0)
        (void)close(accepted);
    if (listener >= 0)
        (void)close(listener);
    if (parent >= 0)
        (void)unlinkat(parent, "fixture-3.service", 0);
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
    tired_text_destroy(&load_directory);
    tired_text_destroy(&user_units);
    return result;
}
