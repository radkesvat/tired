#define _GNU_SOURCE
#include "tired/io.h"
#include "tired/portable.h"
#include "tired/sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool invalid(TiredError *error)
{
    return tired_error_set(
        error, TIRED_CONFLICT, "helper-cache-content",
        "The cached helper has unexpected contents or permissions. "
        "Inspect the helper cache before retrying; no existing file was replaced.",
        0);
}

static bool payload_valid(const TiredEmbeddedFile *file, TiredError *error)
{
    if (file == NULL || file->data == NULL || file->length == 0 || file->sha256 == NULL ||
        strlen(file->sha256) != 64)
        return invalid(error);
    char digest[TIRED_SHA256_HEX_SIZE];
    tired_sha256(file->data, file->length, digest);
    return strcmp(digest, file->sha256) == 0 || invalid(error);
}

static bool verify(TiredDirectory *directory, const TiredEmbeddedFile *file, bool *found,
                   TiredError *error)
{
    *found = false;
    if (!tired_directory_check(directory, error))
        return false;
    int parent = tired_directory_fd(directory);
    int fd = openat(parent, "tired-helper", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
    {
        if (errno == ENOENT)
        {
            tired_error_clear(error);
            return true;
        }
        return tired_error_set(error, TIRED_AUTHORIZATION, "helper-cache-open",
                               "Cannot open the cached helper safely.", errno);
    }
    struct stat status, owner;
    bool ok = fstat(fd, &status) == 0 && fstat(parent, &owner) == 0 && S_ISREG(status.st_mode) &&
              status.st_uid == owner.st_uid && (status.st_mode & 07777) == 0755 &&
              status.st_nlink == 1 && status.st_size >= 0 &&
              (uint64_t)status.st_size == file->length;
    TiredSha256 hash;
    tired_sha256_init(&hash);
    size_t total = 0;
    unsigned char bytes[16384];
    while (ok)
    {
        ssize_t count = read(fd, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            ok = count == 0 && total == file->length;
            break;
        }
        if ((size_t)count > file->length - total)
        {
            ok = false;
            break;
        }
        total += (size_t)count;
        tired_sha256_update(&hash, bytes, (size_t)count);
    }
    char digest[TIRED_SHA256_HEX_SIZE];
    tired_sha256_final(&hash, digest);
    if (close(fd) != 0)
        ok = false;
    if (!ok || strcmp(digest, file->sha256) != 0)
        return invalid(error);
    *found = true;
    tired_error_clear(error);
    return true;
}

bool tired_helper_cache_find(TiredDirectory *directory, const TiredEmbeddedFile *file, bool *found,
                             TiredError *error)
{
    *found = false;
    if (!payload_valid(file, error))
        return false;
    TiredDirectory *version = NULL;
    if (!tired_directory_child(directory, file->sha256, false, false, &version, error))
    {
        if (error->status != TIRED_NOT_FOUND)
            return false;
        tired_error_clear(error);
        return true;
    }
    bool ok = verify(version, file, found, error);
    tired_directory_destroy(version);
    return ok;
}

bool tired_helper_cache_install(TiredDirectory *directory, const TiredEmbeddedFile *file,
                                TiredError *error)
{
    if (!payload_valid(file, error))
        return false;
    TiredDirectory *version = NULL;
    if (!tired_directory_child_mode(directory, file->sha256, true, false, 0755, &version, error))
        return false;
    bool found = false, ok = verify(version, file, &found, error);
    int fd = -1, parent = tired_directory_fd(version);
    char temporary[64] = {0};
    bool staged = false;
    if (!ok || found)
        goto done;
    ok = false;
    char uuid[37];
    if (!tired_uuid_create(uuid, error))
        goto done;
    (void)snprintf(temporary, sizeof(temporary), ".helper-%s", uuid);
    fd = openat(parent, temporary, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        goto io_fail;
    staged = true;
    for (size_t offset = 0; offset < file->length;)
    {
        ssize_t count = write(fd, file->data + offset, file->length - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            goto io_fail;
        offset += (size_t)count;
    }
    if (fchmod(fd, 0755) != 0 || fsync(fd) != 0)
        goto io_fail;
    if (close(fd) != 0)
    {
        fd = -1;
        goto io_fail;
    }
    fd = -1;
    if (!tired_directory_check(directory, error) || !tired_directory_check(version, error))
        goto done;
    if (renameat2(parent, temporary, parent, "tired-helper", RENAME_NOREPLACE) != 0)
    {
        if (errno != EEXIST)
            goto io_fail;
        /* Another authorized process may have installed the exact same build. */
        ok = verify(version, file, &found, error) && found;
        goto done;
    }
    staged = false;
    if (fsync(parent) != 0)
        goto io_fail;
    ok = verify(version, file, &found, error) && found;
    goto done;
io_fail:
    tired_error_set(error, TIRED_RUNTIME_FAILED, "helper-cache-install",
                    "Cannot install the bundled helper in its cache.", errno);
done:
    if (fd >= 0)
        (void)close(fd);
    if (staged)
        (void)unlinkat(parent, temporary, 0);
    tired_directory_destroy(version);
    return ok;
}
