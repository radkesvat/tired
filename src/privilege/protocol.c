#include "tired/build_identity.h"
#include "tired/helper.h"
#include "tired/json.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    char bytes[256];
    int length = snprintf(bytes, sizeof(bytes),
                          "{\"protocol_version\":1,\"ready\":true,\"build_id\":\"%s\"}",
                          tired_build_identity);
    if (length < 0 || (size_t)length >= sizeof(bytes))
        return tired_error_set(error, TIRED_INTERNAL, "helper-identity",
                               "Invalid helper build identity.", 0);
    const TiredText frame = {.data = bytes, .length = (size_t)length};
    return tired_protocol_write(fd, &frame, 5000, error);
}

bool tired_protocol_check_ready(const TiredText *frame, TiredError *error)
{
    struct json_object *handshake = NULL, *version = NULL, *ready = NULL, *identity = NULL;
    uint64_t protocol;
    bool ok = tired_json_parse(frame->data, frame->length, 256, &handshake, error) &&
              json_object_is_type(handshake, json_type_object) &&
              json_object_object_length(handshake) == 3 &&
              json_object_object_get_ex(handshake, "protocol_version", &version) &&
              tired_json_u64(version, 1, 1, &protocol, error) &&
              json_object_object_get_ex(handshake, "ready", &ready) &&
              json_object_is_type(ready, json_type_boolean) && json_object_get_boolean(ready) &&
              json_object_object_get_ex(handshake, "build_id", &identity) &&
              json_object_is_type(identity, json_type_string) &&
              (size_t)json_object_get_string_len(identity) == strlen(tired_build_identity) &&
              strcmp(json_object_get_string(identity), tired_build_identity) == 0;
    json_object_put(handshake);
    if (!ok)
        return tired_error_set(
            error, TIRED_UNSUPPORTED, "helper-version",
            "The frontend and helper builds do not match. Reinstall the matching package.", 0);
    tired_error_clear(error);
    return true;
}
