#include "tired/io.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

bool tired_path_absolute(const TiredText *directory, const char *path, size_t length,
                         TiredText *output, TiredError *error)
{
    assert(directory != NULL && output != NULL && (path != NULL || length == 0));
    if (length == 0 || directory->length == 0 || directory->data[0] != '/')
        return tired_error_set(error, TIRED_INVALID, "path",
                               "Expected a path and an absolute captured directory.", 0);
    if (length > TIRED_INPUT_LIMIT || !tired_validate_text(path, length, true, error))
        return tired_error_set(error, TIRED_INVALID, "path",
                               "Path exceeds its limit or contains unsupported text.", 0);
    if (path[0] == '/')
        return tired_text_set(output, path, length, TIRED_INPUT_LIMIT, error);
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = tired_buffer_append(&buffer, directory->data, directory->length, error) &&
              tired_buffer_append(&buffer, "/", 1, error) &&
              tired_buffer_append(&buffer, path, length, error) &&
              tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}

bool tired_read_file(const char *path, size_t limit, TiredText *output, TiredError *error)
{
    assert(path != NULL && output != NULL && limit < SIZE_MAX);
    int fd;
    do
    {
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return tired_error_set(error, TIRED_INVALID, "input-open",
                               "Cannot open the requested input file.", errno);
    TiredText staged = {0};
    bool ok = tired_read_fd(fd, limit, &staged, error);
    if (close(fd) != 0 && ok)
        ok =
            tired_error_set(error, TIRED_INVALID, "input-close", "Cannot close input file.", errno);
    if (ok)
    {
        tired_text_destroy(output);
        *output = staged;
    }
    else
        tired_text_destroy(&staged);
    return ok;
}

bool tired_read_fd(int fd, size_t limit, TiredText *output, TiredError *error)
{
    assert(fd >= 0 && output != NULL && limit < SIZE_MAX);
    struct stat before;
    if (fstat(fd, &before) != 0)
    {
        int saved_errno = errno;
        return tired_error_set(error, TIRED_INVALID, "input-stat", "Cannot inspect input file.",
                               saved_errno);
    }
    if (!S_ISREG(before.st_mode) || before.st_size < 0 || (uintmax_t)before.st_size > limit)
    {
        return tired_error_set(error, TIRED_INVALID, "input-file",
                               "Input must be a regular file within the byte limit.", 0);
    }
    TiredBuffer buffer;
    tired_buffer_init(&buffer, limit);
    char chunk[4096];
    bool ok = true;
    for (;;)
    {
        size_t remaining = limit - buffer.length;
        size_t wanted = remaining < sizeof(chunk) ? remaining + 1 : sizeof(chunk);
        ssize_t count = read(fd, chunk, wanted);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
        {
            ok = tired_error_set(error, TIRED_INVALID, "input-read", "Cannot read input file.",
                                 errno);
            break;
        }
        if (count == 0)
            break;
        if (!tired_buffer_append(&buffer, chunk, (size_t)count, error))
        {
            ok = false;
            break;
        }
    }
    struct stat after;
    if (ok && fstat(fd, &after) != 0)
        ok = tired_error_set(error, TIRED_INVALID, "input-stat", "Cannot recheck input file.",
                             errno);
    if (ok && (before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
               before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
               before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
               before.st_ctim.tv_nsec != after.st_ctim.tv_nsec))
        ok = tired_error_set(error, TIRED_CONFLICT, "input-changed",
                             "Input file changed while being read; retry with a stable file.", 0);
    if (ok)
        ok = tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}

bool tired_write_private_new(const char *path, const char *data, size_t length, TiredError *error)
{
    assert(path != NULL && (data != NULL || length == 0));
    int fd;
    do
    {
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, 0600);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return tired_error_set(
            error, errno == EEXIST ? TIRED_CONFLICT : TIRED_INVALID, "export-create",
            "Cannot create a new private output; existing paths are never replaced.", errno);
    size_t written = 0;
    int failure = 0;
    while (written < length)
    {
        size_t count = length - written;
        if (count > 65536)
            count = 65536;
        ssize_t result = write(fd, data + written, count);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
        {
            failure = result == 0 ? EIO : errno;
            break;
        }
        written += (size_t)result;
    }
    if (failure == 0)
    {
        int rc;
        do
        {
            rc = fsync(fd);
        } while (rc != 0 && errno == EINTR);
        if (rc != 0)
            failure = errno;
    }
    if (close(fd) != 0 && failure == 0)
        failure = errno;
    if (failure != 0)
        return tired_error_set(
            error, TIRED_INTERNAL, "export-incomplete",
            "Private export failed; a partial file may remain at the requested path.", failure);
    tired_error_clear(error);
    return true;
}

bool tired_uuid_create(char output[37], TiredError *error)
{
    assert(output != NULL);
    unsigned char bytes[16];
    size_t have = 0;
    while (have < sizeof(bytes))
    {
        ssize_t result = getrandom(bytes + have, sizeof(bytes) - have, 0);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return tired_error_set(error, TIRED_INTERNAL, "random-source",
                                   "Cannot obtain kernel randomness for a service identifier.",
                                   result == 0 ? EIO : errno);
        have += (size_t)result;
    }
    bytes[6] = (unsigned char)((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = (unsigned char)((bytes[8] & 0x3fU) | 0x80U);
    static const char hex[] = "0123456789abcdef";
    size_t at = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            output[at++] = '-';
        output[at++] = hex[bytes[i] >> 4];
        output[at++] = hex[bytes[i] & 15];
    }
    output[at] = '\0';
    tired_error_clear(error);
    return true;
}

bool tired_uuid_valid(const char *data, size_t length)
{
    assert(data != NULL || length == 0);
    if (length != 36 || data[14] != '4' ||
        (data[19] != '8' && data[19] != '9' && data[19] != 'a' && data[19] != 'b'))
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        char c = data[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
                return false;
        }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}
