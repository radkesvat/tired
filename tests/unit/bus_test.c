#include "tired/io.h"
#include "tired/linger_enable.h"
#include "tired/linger_query.h"
#include "tired/load_paths.h"
#include "tired/manager.h"
#include "tired/manager_enablement.h"
#include "tired/manager_identity.h"
#include "tired/manager_job.h"
#include "tired/manager_reload.h"
#include "tired/name_query.h"
#include "tired/unit_batch.h"
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
    unsigned configuration_loads;
    unsigned path_mode;
    unsigned reload_case;
    unsigned job_case;
    unsigned enablement_case;
    unsigned linger_case;
    unsigned linger_enable_case;
    bool job_match, subscribed;
    unsigned name_mode, name_requests;
    const char *name_directory;
    bool matched, change_during_uid, silent_match, pinned_version, login;
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
        rc = sd_bus_message_append(message, "{sv}", "ActiveState", "s",
                                   broker->unit_case == 2 ? "inactive" : "active");
    if (rc >= 0 && !service)
        rc = sd_bus_message_append(message, "{sv}", "DropInPaths", "as", 1,
                                   "/etc/systemd/system/fixture.service.d/custom.conf");
    if (rc >= 0)
        rc = sd_bus_message_close_container(message);
    if (rc >= 0)
        rc = sd_bus_send(sd_bus_message_get_bus(request), message, NULL);
    sd_bus_message_unref(message);
    return rc;
}
static int changed_service(sd_bus *bus, const char *service)
{
    sd_bus_message *message = NULL;
    int rc = sd_bus_message_new_signal(bus, &message, "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus", "NameOwnerChanged");
    if (rc >= 0)
        rc = sd_bus_message_set_sender(message, "org.freedesktop.DBus");
    if (rc >= 0)
        rc = sd_bus_message_append(message, "sss", service, ":1.42", ":1.43");
    if (rc >= 0)
        rc = sd_bus_send(bus, message, NULL);
    sd_bus_message_unref(message);
    return rc;
}
static int changed(sd_bus *bus) { return changed_service(bus, "org.freedesktop.systemd1"); }
static int job_signal(sd_bus *bus, uint32_t id, const char *result)
{
    sd_bus_message *message = NULL;
    char path[64];
    (void)snprintf(path, sizeof(path), "/org/freedesktop/systemd1/job/%u", id);
    int rc = sd_bus_message_new_signal(bus, &message, "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager", "JobRemoved");
    if (rc >= 0)
        rc = sd_bus_message_set_sender(message, ":1.42");
    if (rc >= 0)
        rc = sd_bus_message_append(message, "uoss", id, path,
                                   id == 999 ? "unrelated.target" : "fixture.service", result);
    if (rc >= 0)
        rc = sd_bus_send(bus, message, NULL);
    sd_bus_message_unref(message);
    return rc;
}
static int respond(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    Broker *broker = userdata;
    (void)error;
    if (sd_bus_message_is_method_call(message, "org.freedesktop.login1.Manager", "SetUserLinger") >
        0)
    {
        uint32_t uid;
        int enabled, interactive;
        const char *destination = sd_bus_message_get_destination(message);
        if (!broker->login || destination == NULL || strcmp(destination, ":1.42") != 0 ||
            sd_bus_message_get_auto_start(message) != 0 ||
            sd_bus_message_read(message, "ubb", &uid, &enabled, &interactive) <= 0 || uid != 1000 ||
            enabled != 1 || interactive != (broker->linger_enable_case == 1) ||
            sd_bus_message_get_allow_interactive_authorization(message) != interactive)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Invalid lingering enable request");
        if (broker->linger_enable_case == 2)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
        if (broker->linger_enable_case == 3)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_UNKNOWN_METHOD, "Unsupported");
        if (broker->linger_enable_case == 4)
            return 1;
        if (broker->linger_enable_case == 5)
            return sd_bus_reply_method_return(message, "b", 1);
        if (broker->linger_enable_case == 11)
            return sd_bus_reply_method_errorf(message, "org.freedesktop.login1.NoSuchUser",
                                              "Missing");
        return sd_bus_reply_method_return(message, "");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.login1.Manager", "GetUser") > 0)
    {
        const char *destination = sd_bus_message_get_destination(message);
        uint32_t uid;
        if (!broker->login || destination == NULL || strcmp(destination, ":1.42") != 0 ||
            sd_bus_message_get_auto_start(message) != 0 ||
            sd_bus_message_get_allow_interactive_authorization(message) != 0 ||
            sd_bus_message_read(message, "u", &uid) <= 0 || uid != 1000)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Invalid account request");
        if (broker->linger_case == 2)
            return sd_bus_reply_method_errorf(message, "org.freedesktop.login1.NoSuchUser",
                                              "Missing");
        if (broker->linger_case == 5)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
        if (broker->linger_case == 7)
            return 1;
        if (broker->linger_case == 9)
            return sd_bus_reply_method_return(message, "s", "/wrong/type");
        return sd_bus_reply_method_return(message, "o", "/org/freedesktop/login1/user/fixture");
    }
    bool enabling = sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                  "EnableUnitFiles") > 0;
    bool disabling = sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                   "DisableUnitFiles") > 0;
    if (enabling || disabling)
    {
        const char *destination = sd_bus_message_get_destination(message), *unit = NULL;
        int runtime = 1, force = 0;
        if (destination == NULL || strcmp(destination, ":1.42") != 0 ||
            sd_bus_message_get_auto_start(message) != 0 ||
            sd_bus_message_get_allow_interactive_authorization(message) != 0 ||
            sd_bus_message_has_signature(message, enabling ? "asbb" : "asb") <= 0 ||
            sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "s") <= 0 ||
            sd_bus_message_read(message, "s", &unit) <= 0 || strcmp(unit, "fixture.service") != 0 ||
            sd_bus_message_at_end(message, false) <= 0 ||
            sd_bus_message_exit_container(message) < 0 ||
            sd_bus_message_read(message, "b", &runtime) <= 0 ||
            (enabling && sd_bus_message_read(message, "b", &force) <= 0) || runtime || force ||
            sd_bus_message_at_end(message, true) <= 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Bad enablement request");
        if (broker->enablement_case == 2)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
        if (broker->enablement_case == 3)
            return sd_bus_reply_method_return(message, "s", "wrong");
        if (broker->enablement_case == 4)
            return 1;
        sd_bus_message *response = NULL;
        int rc = sd_bus_message_new_method_return(message, &response);
        if (rc >= 0 && enabling)
            rc = sd_bus_message_append(response, "b", broker->enablement_case != 6);
        if (rc >= 0)
            rc = sd_bus_message_open_container(response, SD_BUS_TYPE_ARRAY, "(sss)");
        unsigned count = broker->enablement_case == 6 ? 0 : broker->enablement_case == 8 ? 257 : 1;
        for (unsigned i = 0; rc >= 0 && i < count; ++i)
            rc = sd_bus_message_append(
                response, "(sss)",
                broker->enablement_case == 7 ? "future-change"
                : enabling                   ? "symlink"
                                             : "unlink",
                broker->enablement_case == 9
                    ? "relative"
                    : "/etc/systemd/system/multi-user.target.wants/fixture.service",
                enabling ? "/etc/systemd/system/fixture.service" : "");
        if (rc >= 0)
            rc = sd_bus_message_close_container(response);
        if (rc >= 0)
            rc = sd_bus_send(sd_bus_message_get_bus(message), response, NULL);
        sd_bus_message_unref(response);
        return rc;
    }
    bool subscribe =
        sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager", "Subscribe") > 0;
    bool job_call = sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                  "StartUnit") > 0 ||
                    sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                  "StopUnit") > 0 ||
                    sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                  "RestartUnit") > 0;
    if (subscribe || job_call)
    {
        const char *destination = sd_bus_message_get_destination(message);
        if (!broker->job_match || destination == NULL || strcmp(destination, ":1.42") != 0 ||
            sd_bus_message_get_auto_start(message) != 0 ||
            sd_bus_message_get_allow_interactive_authorization(message) != 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Bad job setup");
        if (subscribe)
        {
            if (broker->job_case == 9)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
            if (broker->subscribed)
                return sd_bus_reply_method_errorf(
                    message, "org.freedesktop.systemd1.AlreadySubscribed", "Subscribed");
            broker->subscribed = true;
            return sd_bus_reply_method_return(message, "");
        }
        const char *unit = NULL, *mode = NULL;
        if (!broker->subscribed || sd_bus_message_read(message, "ss", &unit, &mode) <= 0 ||
            strcmp(unit, "fixture.service") != 0 || strcmp(mode, "replace") != 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Bad job request");
        if (broker->job_case == 3)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
        if (broker->job_case == 4)
            return 1;
        sd_bus *bus = sd_bus_message_get_bus(message);
        if (broker->job_case == 1)
        {
            int rc = sd_bus_reply_method_return(message, "o", "/org/freedesktop/systemd1/job/1");
            return rc < 0 ? rc : job_signal(bus, 1, "done");
        }
        if (broker->job_case == 7 && job_signal(bus, 2, "failed") < 0)
            return -1;
        if (broker->job_case == 7 && job_signal(bus, 999, "failed") < 0)
            return -1;
        if (broker->job_case == 8)
            for (uint32_t i = 100; i < 133; ++i)
                if (job_signal(bus, i, "done") < 0)
                    return -1;
        if (job_signal(bus, 1, broker->job_case == 2 ? "failed" : "done") < 0)
            return -1;
        return sd_bus_reply_method_return(message, "o", "/org/freedesktop/systemd1/job/1");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager", "Reload") > 0)
    {
        const char *destination = sd_bus_message_get_destination(message);
        if (destination == NULL || strcmp(destination, ":1.42") != 0 ||
            sd_bus_message_get_auto_start(message) != 0 ||
            sd_bus_message_get_allow_interactive_authorization(message) != 0 ||
            sd_bus_message_has_signature(message, "") <= 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Invalid reload");
        if (broker->reload_case == 1)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
        if (broker->reload_case == 2)
            return sd_bus_reply_method_return(message, "s", "malformed");
        if (broker->reload_case == 3)
            return 1;
        return sd_bus_reply_method_return(message, "");
    }
    bool file_query = sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager",
                                                    "GetUnitFileState") > 0;
    bool object_query =
        sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager", "GetUnit") > 0;
    bool load_query =
        sd_bus_message_is_method_call(message, "org.freedesktop.systemd1.Manager", "LoadUnit") > 0;
    bool property_query =
        sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "GetAll") > 0;
    if (file_query || object_query || load_query || property_query)
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
        if (load_query)
        {
            ++broker->configuration_loads;
            if (broker->unit_case == 9)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED, "Denied");
            if (broker->unit_case == 10 || broker->unit_case == 12)
                return 1;
            if (broker->unit_case == 11)
                return sd_bus_reply_method_return(message, "o", "/outside");
        }
        if (broker->unit_case == 1 || (broker->unit_case == 2 && !load_query))
            return sd_bus_reply_method_errorf(message, "org.freedesktop.systemd1.NoSuchUnit",
                                              "Absent");
        return sd_bus_reply_method_return(message, "o",
                                          "/org/freedesktop/systemd1/unit/fixture_2eservice");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "Hello") > 0)
        return sd_bus_reply_method_return(message, "s", ":1.99");
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "AddMatch") > 0)
    {
        const char *rule = NULL;
        if (sd_bus_message_read(message, "s", &rule) <= 0)
            return -1;
        if (broker->login && strstr(rule, "arg0='org.freedesktop.login1'") == NULL)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Wrong login match");
        if (strstr(rule, "JobRemoved") != NULL)
            broker->job_match = true;
        broker->matched = true;
        return broker->silent_match ? 1 : sd_bus_reply_method_return(message, "");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "RemoveMatch") > 0)
        return sd_bus_reply_method_return(message, "");
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "GetNameOwner") > 0)
    {
        const char *service = NULL;
        if (sd_bus_message_read(message, "s", &service) <= 0 ||
            strcmp(service,
                   broker->login ? "org.freedesktop.login1" : "org.freedesktop.systemd1") != 0)
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                              "Wrong owner query");
        return broker->matched
                   ? sd_bus_reply_method_return(message, "s", ":1.42")
                   : sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED, "Match missing");
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus", "GetConnectionUnixUser") > 0)
    {
        if (broker->change_during_uid &&
            changed_service(sd_bus_message_get_bus(message),
                            broker->login ? "org.freedesktop.login1" : "org.freedesktop.systemd1") <
                0)
            return -1;
        return sd_bus_reply_method_return(message, "u", broker->uid);
    }
    if (sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get") > 0)
    {
        const char *destination = sd_bus_message_get_destination(message);
        const char *interface = NULL, *property = NULL;
        if (sd_bus_message_read(message, "ss", &interface, &property) <= 0)
            return -1;
        if (broker->login)
        {
            const char *path = sd_bus_message_get_path(message);
            if (destination == NULL || strcmp(destination, ":1.42") != 0 || path == NULL ||
                strcmp(path, "/org/freedesktop/login1/user/fixture") != 0 ||
                strcmp(interface, "org.freedesktop.login1.User") != 0 ||
                sd_bus_message_get_auto_start(message) != 0 ||
                sd_bus_message_get_allow_interactive_authorization(message) != 0)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                                  "Invalid account property request");
            if (strcmp(property, "UID") == 0)
                return sd_bus_reply_method_return(message, "v", "u",
                                                  broker->linger_case == 3 ? 1001U : 1000U);
            if (strcmp(property, "Linger") != 0)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
                                                  "Unexpected property");
            if (broker->linger_case == 6)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_UNKNOWN_PROPERTY,
                                                  "Unavailable");
            if (broker->linger_case == 10)
                return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_UNKNOWN_OBJECT, "Gone");
            if (broker->linger_case == 4)
                return sd_bus_reply_method_return(message, "v", "s", "true");
            return sd_bus_reply_method_return(message, "v", "b", broker->linger_case != 1);
        }
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
    TiredManagerReload *reload = NULL;
    TiredManagerJob *job = NULL;
    TiredManagerEnablement *enablement = NULL;
    TiredManagerIdentity *identity = NULL;
    TiredLingerQuery *linger = NULL;
    TiredLingerEnable *linger_enable = NULL;
    TiredUnitQuery *query = NULL;
    TiredLoadPaths *paths = NULL;
    TiredNameQuery *names = NULL;
    TiredNameQuery *discovery = NULL;
    TiredUnitBatch *batch = NULL;
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
            CHECK(!tired_linger_query_start(identity, 1000, 1000, &linger, &error) &&
                  linger == NULL);
            CHECK(!tired_linger_enable_start(identity, 1000, false, 1000, &linger_enable, &error) &&
                  linger_enable == NULL);
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
            for (unsigned scenario = 0; scenario < 11; ++scenario)
            {
                broker.enablement_case = scenario;
                bool enable = scenario != 1 && scenario != 7;
                CHECK(tired_manager_enablement_start(identity, "fixture.service", enable,
                                                     scenario == 4 ? 20 : 1000, &enablement,
                                                     &error));
                if (scenario == 5)
                    tired_manager_enablement_cancel(enablement);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_manager_enablement_step(enablement))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredManagerEnablementResult response = tired_manager_enablement_result(enablement);
                const TiredStatus expected[] = {TIRED_OK,
                                                TIRED_OK,
                                                TIRED_AUTHORIZATION,
                                                TIRED_INVALID,
                                                TIRED_RUNTIME_FAILED,
                                                TIRED_CANCELLED,
                                                TIRED_OK,
                                                TIRED_OK,
                                                TIRED_INVALID,
                                                TIRED_INVALID,
                                                TIRED_OK};
                CHECK(response.done && response.submitted &&
                      response.error.status == expected[scenario]);
                CHECK(response.acknowledged == (expected[scenario] == TIRED_OK));
                if (response.acknowledged)
                {
                    CHECK(response.changes != NULL &&
                          response.changes->install_info_known == enable);
                    CHECK(response.changes->count == (scenario == 6 ? 0U : 1U));
                    if (scenario == 6)
                        CHECK(!response.changes->carries_install_info);
                    if (scenario == 7)
                        CHECK(strcmp(response.changes->items[0].type.data, "future-change") == 0);
                    if (scenario == 1)
                        CHECK(response.changes->items[0].source.length == 0);
                }
                else
                    CHECK(response.changes == NULL);
                if (scenario != 10)
                {
                    tired_manager_enablement_destroy(enablement);
                    enablement = NULL;
                }
            }
            for (unsigned job_case = 0; job_case < 11; ++job_case)
            {
                broker.job_case = job_case;
                broker.job_match = false;
                CHECK(tired_manager_job_start(identity, "fixture.service",
                                              (TiredJobAction)(job_case % 3),
                                              job_case == 4 ? 50 : 1000, &job, &error));
                if (job_case == 5)
                    tired_manager_job_cancel(job);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (job_case == 6 && tired_manager_job_result(job).submitted)
                        tired_manager_job_cancel(job);
                    if (tired_manager_job_step(job))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredManagerJobResult completed = tired_manager_job_result(job);
                const TiredStatus expected[] = {TIRED_OK,
                                                TIRED_OK,
                                                TIRED_RUNTIME_FAILED,
                                                TIRED_AUTHORIZATION,
                                                TIRED_RUNTIME_FAILED,
                                                TIRED_CANCELLED,
                                                TIRED_CANCELLED,
                                                TIRED_OK,
                                                TIRED_RECOVERY_REQUIRED,
                                                TIRED_AUTHORIZATION,
                                                TIRED_OK};
                if (!completed.done || completed.error.status != expected[job_case])
                    fprintf(
                        stderr,
                        "job case %u: status=%d submitted=%d accepted=%d finished=%d error=%s\n",
                        job_case, completed.error.status, completed.submitted, completed.accepted,
                        completed.finished,
                        completed.error.code == NULL ? "none" : completed.error.code);
                CHECK(completed.done && completed.error.status == expected[job_case]);
                CHECK(completed.submitted == (job_case != 5 && job_case != 9));
                if (expected[job_case] == TIRED_OK || job_case == 2)
                    CHECK(completed.accepted && completed.finished && completed.job_id == 1 &&
                          completed.completion.outcome ==
                              (job_case == 2 ? TIRED_JOB_FAILED : TIRED_JOB_DONE));
                if (job_case != 10)
                {
                    tired_manager_job_destroy(job);
                    job = NULL;
                }
            }
            for (unsigned reload_case = 0; reload_case < 6; ++reload_case)
            {
                broker.reload_case = reload_case;
                CHECK(tired_manager_reload_start(identity, reload_case == 3 ? 20 : 1000, &reload,
                                                 &error));
                struct pollfd descriptor;
                uint64_t deadline;
                CHECK(tired_manager_reload_poll(reload, &descriptor, &deadline, &error));
                CHECK(descriptor.fd >= 0 && deadline != UINT64_MAX);
                if (reload_case == 4)
                    tired_manager_reload_cancel(reload);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_manager_reload_step(reload))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredManagerReloadResult reloaded = tired_manager_reload_result(reload);
                const TiredStatus expected[] = {TIRED_OK,        TIRED_AUTHORIZATION,
                                                TIRED_INVALID,   TIRED_RUNTIME_FAILED,
                                                TIRED_CANCELLED, TIRED_OK};
                CHECK(reloaded.done && reloaded.submitted &&
                      reloaded.error.status == expected[reload_case]);
                CHECK(reloaded.acknowledged == (expected[reload_case] == TIRED_OK));
                if (reload_case != 5)
                {
                    tired_manager_reload_destroy(reload);
                    reload = NULL;
                }
            }
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
                CHECK(!observation.configuration_load_queued &&
                      !observation.configuration_load_acknowledged);
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
            CHECK(broker.configuration_loads == 0);
            CHECK(!tired_unit_query_start_lookup(identity, &unit_base, (TiredUnitLookup)99, 1000,
                                                 &query, &error));
            const unsigned load_cases[] = {2, 1, 9, 10, 11, 3, 12};
            for (size_t load_case = 0; load_case < sizeof(load_cases) / sizeof(load_cases[0]);
                 ++load_case)
            {
                broker.unit_case = load_cases[load_case];
                CHECK(tired_unit_query_start_lookup(
                    identity, &unit_base, TIRED_UNIT_LOAD_CONFIGURATION,
                    broker.unit_case == 10 ? 20 : 1000, &query, &error));
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    bool done = tired_unit_query_step(query);
                    TiredUnitQueryResult current = tired_unit_query_result(query);
                    if (broker.unit_case == 12 && current.configuration_load_queued)
                    {
                        tired_unit_query_cancel(query);
                        break;
                    }
                    if (done)
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredUnitQueryResult observed = tired_unit_query_result(query);
                CHECK(observed.done && observed.configuration_load_queued);
                const TiredStatus expected[] = {
                    TIRED_OK,      TIRED_OK,      TIRED_AUTHORIZATION, TIRED_RUNTIME_FAILED,
                    TIRED_INVALID, TIRED_INVALID, TIRED_CANCELLED};
                CHECK(observed.error.status == expected[load_case]);
                CHECK(observed.configuration_load_acknowledged ==
                      (broker.unit_case == 2 || broker.unit_case == 3));
                if (broker.unit_case == 2)
                {
                    CHECK(observed.file_found && observed.object_found &&
                          observed.observation != NULL);
                    CHECK(
                        strcmp(observed.observation->fields[TIRED_OBS_ACTIVE_STATE].value.text.data,
                               "inactive") == 0);
                }
                else if (broker.unit_case == 1)
                    CHECK(!observed.file_found && !observed.object_found);
                else
                    CHECK(observed.observation == NULL);
                tired_unit_query_destroy(query);
                query = NULL;
            }
            broker.unit_case = 0;
            TiredText batch_names[3072];
            for (size_t i = 0; i < 3072; ++i)
                batch_names[i] = (TiredText){.data = "fixture.service", .length = 15};
            TiredTextList list = {.items = batch_names};
            CHECK(tired_unit_batch_start(identity, &list, 1000, &batch, &error));
            CHECK(tired_unit_batch_result(batch).done && tired_unit_batch_result(batch).count == 0);
            tired_unit_batch_destroy(batch);
            batch = NULL;
            list.count = 3073;
            CHECK(!tired_unit_batch_start(identity, &list, 1000, &batch, &error));
            CHECK(batch == NULL);
            list.count = 3072;
            CHECK(tired_unit_batch_start(identity, &list, 1000, &batch, &error));
            tired_unit_batch_cancel(batch);
            CHECK(tired_unit_batch_result(batch).count == 3072 &&
                  tired_unit_batch_item(batch, 3071).query.error.status == TIRED_CANCELLED);
            tired_unit_batch_destroy(batch);
            batch = NULL;
            const char *bad_names[] = {"../bad.service", "fixture.timer", "unit@instance.service"};
            list.count = 1;
            for (size_t i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); ++i)
            {
                batch_names[0] =
                    (TiredText){.data = (char *)bad_names[i], .length = strlen(bad_names[i])};
                CHECK(!tired_unit_batch_start(identity, &list, 1000, &batch, &error));
                CHECK(batch == NULL);
            }
            batch_names[0] = (TiredText){.data = "fixture.service", .length = 15};
            for (unsigned batch_case = 0; batch_case < 4; ++batch_case)
            {
                list.count = batch_case == 2 ? 10 : 2;
                broker.unit_case = batch_case == 0 ? 5 : batch_case == 2 ? 8 : 0;
                CHECK(tired_unit_batch_start(identity, &list, batch_case == 2 ? 80 : 1000, &batch,
                                             &error));
                CHECK(tired_unit_batch_poll(batch, &poll_descriptor, &poll_deadline, &error));
                CHECK(poll_descriptor.fd >= 0 && poll_deadline != UINT64_MAX);
                if (batch_case == 1)
                    tired_unit_batch_cancel(batch);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_unit_batch_step(batch))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredUnitBatchResult batch_result = tired_unit_batch_result(batch);
                CHECK(batch_result.done && batch_result.count == list.count);
                if (batch_case == 1 || batch_case == 2)
                {
                    CHECK(batch_result.error.status ==
                          (batch_case == 1 ? TIRED_CANCELLED : TIRED_RUNTIME_FAILED));
                    TiredUnitBatchItem last = tired_unit_batch_item(batch, list.count - 1);
                    CHECK(!last.attempted && last.query.done &&
                          last.query.error.status != TIRED_OK && last.query.observation == NULL &&
                          last.completed_realtime_usec == 0);
                }
                else
                {
                    CHECK(batch_result.error.status == TIRED_OK);
                    for (size_t i = 0; i < list.count; ++i)
                    {
                        TiredUnitBatchItem item = tired_unit_batch_item(batch, i);
                        CHECK(item.attempted && item.query.done &&
                              item.completed_realtime_usec > 0);
                        if (batch_case == 0)
                            CHECK(item.query.error.status == TIRED_AUTHORIZATION &&
                                  item.query.observation == NULL);
                        else
                        {
                            CHECK(item.query.error.status == TIRED_OK && item.query.object_found &&
                                  item.query.observation->fields[TIRED_OBS_MAIN_PID]
                                          .value.unsigned_value == 42);
                            CHECK(item.query.observation->fields[TIRED_OBS_DROP_IN_PATHS].known &&
                                  item.query.observation->fields[TIRED_OBS_DROP_IN_PATHS]
                                          .value.list.count == 1);
                        }
                    }
                }
                if (batch_case != 3)
                {
                    tired_unit_batch_destroy(batch);
                    batch = NULL;
                }
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
            CHECK(tired_unit_batch_result(batch).error.status == TIRED_CONFLICT);
            CHECK(tired_unit_batch_item(batch, 0).query.observation == NULL);
            tired_unit_batch_destroy(batch);
            batch = NULL;
            CHECK(tired_name_query_result(names).error.status == TIRED_CONFLICT &&
                  tired_name_query_result(names).unit_name == NULL);
            tired_name_query_destroy(names);
            names = NULL;
            CHECK(tired_load_paths_result(paths).directories == NULL &&
                  tired_load_paths_result(paths).error.status == TIRED_CONFLICT);
            CHECK(tired_manager_reload_result(reload).error.status == TIRED_CONFLICT);
            CHECK(tired_manager_enablement_result(enablement).error.status == TIRED_CONFLICT &&
                  tired_manager_enablement_result(enablement).changes == NULL);
            tired_manager_enablement_destroy(enablement);
            enablement = NULL;
            CHECK(tired_manager_job_result(job).error.status == TIRED_CONFLICT &&
                  tired_manager_job_result(job).finished);
            tired_manager_job_destroy(job);
            job = NULL;
            CHECK(tired_manager_reload_result(reload).acknowledged);
            tired_manager_reload_destroy(reload);
            reload = NULL;
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
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        broker.login = true;
        broker.uid = scenario == 1 ? 1000U : 0U;
        broker.change_during_uid = scenario == 2;
        broker.silent_match = scenario == 3;
        broker.matched = false;
        CHECK(tired_login_identity_start(client, scenario == 3 ? 20 : 1000, &identity, &error));
        for (unsigned i = 0; i < 2000; ++i)
        {
            CHECK(sd_bus_process(server, NULL) >= 0);
            if (tired_manager_identity_step(identity))
                break;
            struct timespec delay = {.tv_nsec = 1000000};
            (void)nanosleep(&delay, NULL);
        }
        TiredManagerIdentityResult owner = tired_manager_identity_result(identity);
        CHECK(owner.kind == TIRED_MANAGER_LOGIN && !owner.user_scope);
        if (scenario == 0)
        {
            CHECK(owner.ready && owner.uid == 0 && owner.error.status == TIRED_OK);
            for (unsigned scenario = 0; scenario < 12; ++scenario)
            {
                broker.linger_enable_case = scenario;
                broker.linger_case = scenario == 6 ? 1 : scenario == 7 ? 6 : scenario == 8 ? 7 : 0;
                CHECK(tired_linger_enable_start(identity, 1000, scenario == 1,
                                                scenario == 4 || scenario == 8 ? 20 : 1000,
                                                &linger_enable, &error));
                struct pollfd descriptor;
                uint64_t deadline;
                CHECK(tired_linger_enable_poll(linger_enable, &descriptor, &deadline, &error) &&
                      descriptor.fd >= 0 && deadline != UINT64_MAX);
                if (scenario == 9)
                    tired_linger_enable_cancel(linger_enable);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_linger_enable_step(linger_enable))
                        break;
                    if (scenario == 10 && tired_linger_enable_result(linger_enable).acknowledged)
                        tired_linger_enable_cancel(linger_enable);
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredLingerEnableResult observed = tired_linger_enable_result(linger_enable);
                const TiredStatus expected[] = {TIRED_OK,
                                                TIRED_OK,
                                                TIRED_AUTHORIZATION,
                                                TIRED_UNSUPPORTED,
                                                TIRED_RUNTIME_FAILED,
                                                TIRED_INVALID,
                                                TIRED_RUNTIME_FAILED,
                                                TIRED_UNSUPPORTED,
                                                TIRED_RUNTIME_FAILED,
                                                TIRED_CANCELLED,
                                                TIRED_CANCELLED,
                                                TIRED_NOT_FOUND};
                CHECK(observed.done && observed.submitted && observed.uid == 1000 &&
                      observed.error.status == expected[scenario]);
                CHECK(observed.acknowledged ==
                      (scenario < 2 || (scenario >= 6 && scenario <= 8) || scenario == 10));
                CHECK(observed.observed == (scenario < 2 || scenario == 6));
                CHECK(observed.enabled == (scenario < 2));
                tired_linger_enable_destroy(linger_enable);
                linger_enable = NULL;
            }
            broker.linger_enable_case = 0;
            for (unsigned scenario = 0; scenario < 11; ++scenario)
            {
                broker.linger_case = scenario;
                CHECK(tired_linger_query_start(identity, 1000, scenario == 7 ? 20 : 1000, &linger,
                                               &error));
                struct pollfd descriptor;
                uint64_t deadline;
                CHECK(tired_linger_query_poll(linger, &descriptor, &deadline, &error) &&
                      descriptor.fd >= 0 && deadline != UINT64_MAX);
                if (scenario == 8)
                    tired_linger_query_cancel(linger);
                for (unsigned i = 0; i < 2000; ++i)
                {
                    CHECK(sd_bus_process(server, NULL) >= 0);
                    if (tired_linger_query_step(linger))
                        break;
                    struct timespec delay = {.tv_nsec = 1000000};
                    (void)nanosleep(&delay, NULL);
                }
                TiredLingerResult observed = tired_linger_query_result(linger);
                const TiredStatus expected[] = {TIRED_OK,          TIRED_OK,
                                                TIRED_NOT_FOUND,   TIRED_CONFLICT,
                                                TIRED_INVALID,     TIRED_AUTHORIZATION,
                                                TIRED_UNSUPPORTED, TIRED_RUNTIME_FAILED,
                                                TIRED_CANCELLED,   TIRED_INVALID,
                                                TIRED_NOT_FOUND};
                CHECK(observed.done && observed.uid == 1000 &&
                      observed.error.status == expected[scenario]);
                CHECK(observed.known == (scenario < 2));
                CHECK(observed.enabled == (scenario == 0));
                tired_linger_query_destroy(linger);
                linger = NULL;
            }
            broker.linger_case = 0;
            CHECK(tired_linger_query_start(identity, 1000, 1000, &linger, &error));
            for (unsigned i = 0; i < 2000; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                if (tired_linger_query_step(linger))
                    break;
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            CHECK(tired_linger_query_result(linger).known &&
                  tired_linger_query_result(linger).enabled);
            CHECK(tired_linger_enable_start(identity, 1000, false, 1000, &linger_enable, &error));
            for (unsigned i = 0; i < 2000; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                if (tired_linger_enable_step(linger_enable))
                    break;
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            CHECK(tired_linger_enable_result(linger_enable).enabled);
            TiredText unit_base = {.data = "fixture", .length = 7};
            TiredTextList pending = {0};
            CHECK(!tired_unit_batch_start(identity, &pending, 1000, &batch, &error) &&
                  batch == NULL);
            CHECK(!tired_unit_query_start(identity, &unit_base, 1000, &query, &error) &&
                  query == NULL);
            CHECK(!tired_load_paths_start(identity, 1000, &paths, &error) && paths == NULL);
            CHECK(!tired_name_query_start(identity, &unit_base, true, &directory, &pending, 1000,
                                          &names, &error) &&
                  names == NULL);
            CHECK(!tired_manager_reload_start(identity, 1000, &reload, &error) && reload == NULL);
            CHECK(!tired_manager_job_start(identity, "fixture.service", TIRED_JOB_START, 1000, &job,
                                           &error) &&
                  job == NULL);
            CHECK(!tired_manager_enablement_start(identity, "fixture.service", true, 1000,
                                                  &enablement, &error) &&
                  enablement == NULL);
            CHECK(changed_service(server, "org.freedesktop.login1") >= 0);
            for (unsigned i = 0; i < 100 && !tired_manager_identity_result(identity).changed; ++i)
            {
                CHECK(sd_bus_process(server, NULL) >= 0);
                (void)tired_manager_identity_step(identity);
                struct timespec delay = {.tv_nsec = 1000000};
                (void)nanosleep(&delay, NULL);
            }
            CHECK(tired_manager_identity_result(identity).changed &&
                  !tired_manager_identity_result(identity).ready);
            CHECK(tired_linger_query_step(linger));
            CHECK(!tired_linger_query_result(linger).known &&
                  tired_linger_query_result(linger).error.status == TIRED_CONFLICT);
            CHECK(tired_linger_enable_result(linger_enable).acknowledged &&
                  !tired_linger_enable_result(linger_enable).observed &&
                  tired_linger_enable_result(linger_enable).error.status == TIRED_CONFLICT);
            tired_linger_enable_destroy(linger_enable);
            linger_enable = NULL;
            tired_linger_query_destroy(linger);
            linger = NULL;
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
    tired_linger_enable_destroy(linger_enable);
    tired_linger_query_destroy(linger);
    tired_manager_enablement_destroy(enablement);
    tired_manager_job_destroy(job);
    tired_manager_reload_destroy(reload);
    tired_unit_batch_destroy(batch);
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
