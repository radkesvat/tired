#define _GNU_SOURCE
#include "tired/capture.h"
#include "tired/manager.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static bool bus_error(TiredError *error, int number)
{
    return tired_error_set(error,
                           number == ENOENT || number == ECONNREFUSED ? TIRED_NOT_FOUND
                           : number == EACCES || number == EPERM      ? TIRED_AUTHORIZATION
                                                                      : TIRED_RUNTIME_FAILED,
                           number == ENOENT || number == ECONNREFUSED ? "bus-unavailable"
                           : number == EACCES || number == EPERM      ? "bus-authorization"
                                                                      : "bus-connect",
                           "Cannot connect to the selected local bus socket.", number);
}
static bool directory_trusted(int fd, uid_t owner, bool private, TiredError *error)
{
    struct stat status;
    if (fstat(fd, &status) != 0)
        return bus_error(error, errno);
    if (!S_ISDIR(status.st_mode) || (status.st_uid != 0 && status.st_uid != owner) ||
        (status.st_mode & 0022) != 0 ||
        (private && (status.st_uid != owner || (status.st_mode & 0077) != 0)))
        return tired_error_set(error, TIRED_CONFLICT, "bus-directory-trust",
                               "Bus directory has unsafe ownership or permissions.", 0);
    return true;
}
bool tired_manager_bus_connect_directory(const char *directory, const char *leaf, bool user_scope,
                                         sd_bus **output, TiredError *error)
{
    assert(directory != NULL && leaf != NULL && output != NULL && *output == NULL);
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "bus-identity",
                               "Bus discovery requires matching real and effective identities.", 0);
    size_t length = strnlen(directory, TIRED_INPUT_LIMIT + 1);
    size_t leaf_length = strnlen(leaf, 256);
    if (length == 0 || length > TIRED_INPUT_LIMIT || directory[0] != '/' ||
        !tired_validate_text(directory, length, true, error) || leaf_length == 0 ||
        leaf_length > 255 || strchr(leaf, '/') != NULL || strcmp(leaf, ".") == 0 ||
        strcmp(leaf, "..") == 0 || !tired_validate_text(leaf, leaf_length, true, error))
        return tired_error_set(error, TIRED_INVALID, "bus-path", "Invalid local bus path.", 0);
    while (length > 1 && directory[length - 1] == '/')
        --length;
    /* Validate all components even if an earlier component is absent. */
    for (size_t position = 1; position < length;)
    {
        size_t end = position;
        while (end < length && directory[end] != '/')
            ++end;
        size_t size = end - position;
        if (size == 0 || size > 255 || (size == 1 && directory[position] == '.') ||
            (size == 2 && directory[position] == '.' && directory[position + 1] == '.'))
            return tired_error_set(error, TIRED_INVALID, "bus-path",
                                   "Bus directory components must be normalized.", 0);
        position = end + 1;
    }
    uid_t owner = user_scope ? getuid() : 0;
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC), socket_fd = -1;
    sd_bus *bus = NULL;
    bool ok = false;
    if (fd < 0)
    {
        bus_error(error, errno);
        goto done;
    }
    if (!directory_trusted(fd, owner, user_scope && length == 1, error))
        goto done;
    for (size_t position = 1; position < length;)
    {
        size_t end = position;
        while (end < length && directory[end] != '/')
            ++end;
        char component[256];
        memcpy(component, directory + position, end - position);
        component[end - position] = '\0';
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int saved = errno;
        (void)close(fd);
        fd = next;
        if (fd < 0)
        {
            bus_error(error, saved);
            goto done;
        }
        if (!directory_trusted(fd, owner, user_scope && end == length, error))
            goto done;
        position = end + 1;
    }
    struct stat status;
    if (fstatat(fd, leaf, &status, AT_SYMLINK_NOFOLLOW) != 0)
    {
        bus_error(error, errno);
        goto done;
    }
    if (!S_ISSOCK(status.st_mode) || status.st_uid != owner)
    {
        tired_error_set(error, TIRED_CONFLICT, "bus-socket-trust",
                        "Bus endpoint is not a socket owned by the expected identity.", 0);
        goto done;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int size =
        snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/%s", fd, leaf);
    if (size < 0 || (size_t)size >= sizeof(address.sun_path))
    {
        tired_error_set(error, TIRED_INVALID, "bus-path",
                        "Bus socket name exceeds the local socket path limit.", 0);
        goto done;
    }
    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (socket_fd < 0 ||
        connect(socket_fd, (const struct sockaddr *)&address,
                (socklen_t)(offsetof(struct sockaddr_un, sun_path) + (size_t)size + 1)) != 0)
    {
        bus_error(error, errno);
        goto done;
    }
    struct ucred peer;
    socklen_t peer_size = sizeof(peer);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0)
    {
        bus_error(error, errno);
        goto done;
    }
    if (peer_size != sizeof(peer) || peer.uid != owner || peer.pid <= 0)
    {
        tired_error_set(error, TIRED_AUTHORIZATION, "bus-peer",
                        "Local bus peer has an unexpected identity.", 0);
        goto done;
    }
    int rc = sd_bus_new(&bus);
    if (rc >= 0)
        rc = sd_bus_set_fd(bus, socket_fd, socket_fd);
    if (rc < 0)
    {
        bus_error(error, -rc);
        goto done;
    }
    socket_fd = -1; /* sd-bus owns it from this point. */
    rc = sd_bus_set_bus_client(bus, 1);
    if (rc >= 0)
        rc = sd_bus_set_allow_interactive_authorization(bus, 0);
    if (rc >= 0)
        rc = sd_bus_set_exit_on_disconnect(bus, 0);
    if (rc >= 0)
        rc = sd_bus_start(bus);
    if (rc < 0)
    {
        bus_error(error, -rc);
        goto done;
    }
    *output = bus;
    bus = NULL;
    tired_error_clear(error);
    ok = true;
done:
    if (fd >= 0)
        (void)close(fd);
    if (socket_fd >= 0)
        (void)close(socket_fd);
    sd_bus_close_unref(bus);
    return ok;
}
bool tired_manager_bus_open(bool user_scope, sd_bus **output, TiredError *error)
{
    assert(output != NULL && *output == NULL);
    if (!user_scope)
        return tired_manager_bus_connect_directory("/run/dbus", "system_bus_socket", false, output,
                                                   error);
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == NULL || runtime[0] == '\0')
        return tired_error_set(
            error, TIRED_NOT_FOUND, "user-runtime",
            "User scope requires an existing XDG_RUNTIME_DIR and user bus; no session was created.",
            0);
    return tired_manager_bus_connect_directory(runtime, "bus", true, output, error);
}
