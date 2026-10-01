#include "tired/file_fingerprint.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include "tired/memory.h"
#include "tired/private_file.h"
#include "tired/sha256.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static bool io_error(TiredError *error)
{
    int number = errno;
    return tired_error_set(
        error, number == EACCES || number == EPERM ? TIRED_AUTHORIZATION : TIRED_RUNTIME_FAILED,
        "fingerprint-io", "Cannot inspect requested file fingerprint.", number);
}
static bool changed(TiredError *error)
{
    return tired_error_set(error, TIRED_CONFLICT, "fingerprint-changed",
                           "File identity or metadata changed during fingerprint inspection.", 0);
}
static bool same(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_mode == b->st_mode &&
           a->st_uid == b->st_uid && a->st_gid == b->st_gid && a->st_nlink == b->st_nlink &&
           a->st_size == b->st_size && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
           a->st_mtim.tv_nsec == b->st_mtim.tv_nsec && a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
           a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
static bool inspect(TiredDirectory *directory, const char *name, size_t limit,
                    TiredFileFingerprint *output, TiredText *bytes, TiredError *error)
{
    assert(directory != NULL && name != NULL && output != NULL);
    size_t length = strnlen(name, 256);
    if (length == 0 || length > 255 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        memchr(name, '/', length) != NULL || !tired_validate_text(name, length, true, error) ||
        limit > TIRED_PRIVATE_FILE_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "fingerprint-input",
                               "Invalid fingerprint file name or byte limit.", 0);
    if (!tired_directory_check(directory, error))
        return false;
    int parent = tired_directory_fd(directory);
    struct stat before, opened, after, entry;
    if (fstatat(parent, name, &before, AT_SYMLINK_NOFOLLOW) != 0)
    {
        if (errno != ENOENT)
            return io_error(error);
        if (!tired_directory_check(directory, error))
            return false;
        *output = (TiredFileFingerprint){0};
        if (bytes != NULL)
            tired_text_destroy(bytes);
        tired_error_clear(error);
        return true;
    }
    if (!S_ISREG(before.st_mode) || before.st_nlink != 1)
        return tired_error_set(error, TIRED_CONFLICT, "fingerprint-file-type",
                               "Fingerprint requires a regular file with one link.", 0);
    if (before.st_size < 0 || (uintmax_t)before.st_size > limit)
        return tired_error_set(error, TIRED_INVALID, "fingerprint-limit",
                               "File exceeds the fingerprint byte limit.", 0);
    int fd = openat(parent, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0)
        return io_error(error);
    TiredSha256 context = {0};
    TiredBuffer contents;
    tired_buffer_init(&contents, limit);
    TiredFileFingerprint result = {0};
    bool ok = false;
    if (fstat(fd, &opened) != 0)
    {
        io_error(error);
        goto done;
    }
    if (!same(&before, &opened))
    {
        changed(error);
        goto done;
    }
    tired_sha256_init(&context);
    unsigned char buffer[65536];
    size_t consumed = 0;
    for (;;)
    {
        size_t wanted = limit - consumed < sizeof(buffer) ? limit - consumed + 1 : sizeof(buffer);
        ssize_t count = read(fd, buffer, wanted);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
        {
            io_error(error);
            goto done;
        }
        if (count == 0)
            break;
        if ((size_t)count > limit - consumed)
        {
            tired_error_set(error, TIRED_INVALID, "fingerprint-limit",
                            "File grew beyond the fingerprint byte limit.", 0);
            goto done;
        }
        consumed += (size_t)count;
        if (bytes != NULL &&
            !tired_buffer_append(&contents, (const char *)buffer, (size_t)count, error))
            goto done;
        tired_sha256_update(&context, buffer, (size_t)count);
    }
    if (fstat(fd, &after) != 0 || fstatat(parent, name, &entry, AT_SYMLINK_NOFOLLOW) != 0)
    {
        io_error(error);
        goto done;
    }
    if (!same(&before, &after) || !same(&after, &entry) || (uintmax_t)after.st_size != consumed)
    {
        changed(error);
        goto done;
    }
    if (!tired_directory_check(directory, error))
        goto done;
    result = (TiredFileFingerprint){.exists = true,
                                    .device = after.st_dev,
                                    .inode = after.st_ino,
                                    .uid = after.st_uid,
                                    .gid = after.st_gid,
                                    .mode = after.st_mode & 07777,
                                    .size = (uint64_t)consumed};
    tired_sha256_final(&context, result.sha256);
    ok = true;
done:
    tired_memory_clear(&context, sizeof(context));
    if (close(fd) != 0 && ok)
        ok = io_error(error);
    if (ok && bytes != NULL)
        ok = tired_buffer_take(&contents, bytes, error);
    if (contents.data != NULL)
        tired_memory_clear(contents.data, contents.length);
    tired_buffer_destroy(&contents);
    if (ok)
    {
        *output = result;
        tired_error_clear(error);
    }
    return ok;
}
bool tired_file_fingerprint(TiredDirectory *directory, const char *name, size_t limit,
                            TiredFileFingerprint *output, TiredError *error)
{
    return inspect(directory, name, limit, output, NULL, error);
}
bool tired_file_snapshot(TiredDirectory *directory, const char *name, size_t limit,
                         TiredFileFingerprint *output, TiredText *bytes, TiredError *error)
{
    assert(bytes != NULL);
    return inspect(directory, name, limit, output, bytes, error);
}
bool tired_file_fingerprint_equal(const TiredFileFingerprint *a, const TiredFileFingerprint *b)
{
    assert(a != NULL && b != NULL);
    if (a->exists != b->exists)
        return false;
    return !a->exists || (a->device == b->device && a->inode == b->inode && a->uid == b->uid &&
                          a->gid == b->gid && a->mode == b->mode && a->size == b->size &&
                          memcmp(a->sha256, b->sha256, 65) == 0);
}
