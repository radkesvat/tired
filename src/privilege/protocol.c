#include "tired/helper.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
static bool transfer(int fd, void *bytes, size_t length, bool writing, uint64_t deadline,
                     const volatile sig_atomic_t *cancel, TiredError *error)
{
    size_t offset = 0;
    while (offset < length)
    {
        if (cancel != NULL && *cancel)
            return tired_error_set(error, TIRED_INTERRUPTED, "helper-cancelled",
                                   "Cancelled before transferring the approved operation.", 0);
        uint64_t now = tired_monotonic_usec();
        if (now == 0 || now >= deadline)
            return tired_error_set(error, TIRED_AUTHORIZATION, "helper-timeout",
                                   "Administrative protocol timed out.", 0);
        struct pollfd descriptor = {.fd = fd, .events = writing ? POLLOUT : POLLIN};
        uint64_t milliseconds = (deadline - now + 999) / 1000;
        int rc = poll(&descriptor, 1, milliseconds > 1000 ? 1000 : (int)milliseconds);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0)
            return tired_error_set(error, TIRED_INTERNAL, "helper-poll",
                                   "Cannot wait for administrative IPC.", errno);
        if (rc == 0)
            continue;
        ssize_t count = writing ? write(fd, (char *)bytes + offset, length - offset)
                                : read(fd, (char *)bytes + offset, length - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count <= 0)
            return tired_error_set(error, TIRED_AUTHORIZATION, "helper-short-frame",
                                   "Administrative IPC ended before the complete frame.",
                                   count < 0 ? errno : 0);
        offset += (size_t)count;
    }
    return true;
}
bool tired_protocol_read_interruptible(int fd, unsigned timeout_ms,
                                       const volatile sig_atomic_t *cancel, TiredText *output,
                                       TiredError *error)
{
    uint64_t deadline = tired_monotonic_usec() + (uint64_t)timeout_ms * 1000;
    uint32_t wire;
    if (!transfer(fd, &wire, sizeof(wire), false, deadline, cancel, error))
        return false;
    size_t length = ntohl(wire);
    if (length == 0 || length > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "helper-frame-limit",
                               "Administrative frame is empty or exceeds 1 MiB.", 0);
    char *bytes = malloc(length);
    if (bytes == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot receive administrative request.", 0);
    bool ok = transfer(fd, bytes, length, false, deadline, cancel, error) &&
              tired_text_set(output, bytes, length, TIRED_INPUT_LIMIT, error);
    free(bytes);
    return ok;
}
bool tired_protocol_write_interruptible(int fd, const TiredText *bytes, unsigned timeout_ms,
                                        const volatile sig_atomic_t *cancel, TiredError *error)
{
    if (bytes->length == 0 || bytes->length > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "helper-frame-limit",
                               "Administrative frame exceeds 1 MiB.", 0);
    uint64_t deadline = tired_monotonic_usec() + (uint64_t)timeout_ms * 1000;
    uint32_t wire = htonl((uint32_t)bytes->length);
    return transfer(fd, &wire, sizeof(wire), true, deadline, cancel, error) &&
           transfer(fd, bytes->data, bytes->length, true, deadline, cancel, error);
}

bool tired_protocol_read(int fd, unsigned timeout_ms, TiredText *output, TiredError *error)
{
    return tired_protocol_read_interruptible(fd, timeout_ms, NULL, output, error);
}
bool tired_protocol_write(int fd, const TiredText *bytes, unsigned timeout_ms, TiredError *error)
{
    return tired_protocol_write_interruptible(fd, bytes, timeout_ms, NULL, error);
}
bool tired_protocol_ready(int fd, TiredError *error)
{
    const char bytes[] = "{\"protocol_version\":1,\"ready\":true}";
    const TiredText frame = {.data = (char *)bytes, .length = sizeof(bytes) - 1};
    return tired_protocol_write(fd, &frame, 5000, error);
}
