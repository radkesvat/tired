#include "tired/directory.h"
#include "tired/capture.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct TiredDirectory
{
    int fd, parent;
    uid_t owner;
    bool private_directory;
    char name[256];
};
static bool io_error(TiredError *error, const char *code, const char *message)
{
    int number = errno;
    TiredStatus status = number == ENOENT                      ? TIRED_NOT_FOUND
                         : number == EACCES || number == EPERM ? TIRED_AUTHORIZATION
                                                               : TIRED_RUNTIME_FAILED;
    return tired_error_set(error, status, code, message, number);
}
static bool component(const char *name, size_t length, TiredError *error)
{
    if (length == 0 || length > 255 || (length == 1 && name[0] == '.') ||
        (length == 2 && name[0] == '.' && name[1] == '.') || memchr(name, '/', length) != NULL ||
        !tired_validate_text(name, length, true, error))
        return tired_error_set(error, TIRED_INVALID, "directory-component",
                               "Directory components must be bounded normalized names.", 0);
    return true;
}
static bool trusted(int fd, uid_t owner, bool private_directory, struct stat *status,
                    TiredError *error)
{
    if (fstat(fd, status) != 0)
        return io_error(error, "directory-stat", "Cannot inspect storage directory.");
    if (!S_ISDIR(status->st_mode) || (status->st_uid != 0 && status->st_uid != owner) ||
        (status->st_mode & 0022) != 0 ||
        (private_directory && (status->st_uid != owner || (status->st_mode & 07777) != 0700)))
        return tired_error_set(error, TIRED_CONFLICT, "directory-trust",
                               "Storage directory has unsafe ownership or permissions.", 0);
    return true;
}
bool tired_directory_check(const TiredDirectory *directory, TiredError *error)
{
    assert(directory != NULL);
    struct stat current, binding, parent;
    if (!trusted(directory->fd, directory->owner, directory->private_directory, &current, error))
        return false;
    if (directory->parent >= 0)
    {
        if (!trusted(directory->parent, directory->owner, false, &parent, error))
            return false;
        if (fstatat(directory->parent, directory->name, &binding, AT_SYMLINK_NOFOLLOW) != 0)
            return io_error(error, "directory-binding",
                            "Cannot inspect storage directory binding.");
        if (!S_ISDIR(binding.st_mode) || current.st_dev != binding.st_dev ||
            current.st_ino != binding.st_ino)
            return tired_error_set(error, TIRED_CONFLICT, "directory-replaced",
                                   "Storage directory was replaced; operation refused.", 0);
    }
    tired_error_clear(error);
    return true;
}
static bool own(int fd, int parent, const char *name, uid_t owner, bool private_directory,
                TiredDirectory **output, TiredError *error)
{
    TiredDirectory *directory = calloc(1, sizeof(*directory));
    if (directory == NULL)
    {
        tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate storage directory.",
                        errno);
        (void)close(fd);
        if (parent >= 0)
            (void)close(parent);
        return false;
    }
    directory->fd = fd;
    directory->parent = parent;
    directory->owner = owner;
    directory->private_directory = private_directory;
    if (name != NULL)
        memcpy(directory->name, name, strlen(name) + 1);
    if (!tired_directory_check(directory, error))
    {
        tired_directory_destroy(directory);
        return false;
    }
    *output = directory;
    return true;
}
bool tired_directory_open(const char *path, uid_t owner, bool private_directory,
                          TiredDirectory **output, TiredError *error)
{
    assert(path != NULL && output != NULL && *output == NULL);
    size_t length = strnlen(path, 4097);
    if (length == 0 || length > 4096 || path[0] != '/' || (length > 1 && path[length - 1] == '/'))
        return tired_error_set(error, TIRED_INVALID, "directory-path",
                               "Expected a bounded normalized absolute directory path.", 0);
    /* Validate the whole path before an absent prefix can mask malformed input. */
    for (size_t start = 1; start < length;)
    {
        size_t end = start;
        while (end < length && path[end] != '/')
            ++end;
        if (!component(path + start, end - start, error))
            return false;
        start = end + 1;
    }
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return io_error(error, "directory-root", "Cannot open filesystem root.");
    struct stat status;
    if (!trusted(fd, owner, false, &status, error))
    {
        (void)close(fd);
        return false;
    }
    if (length == 1)
        return own(fd, -1, NULL, owner, private_directory, output, error);
    for (size_t start = 1; start < length;)
    {
        size_t end = start;
        while (end < length && path[end] != '/')
            ++end;
        char name[256];
        memcpy(name, path + start, end - start);
        name[end - start] = '\0';
        int next = openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (next < 0)
        {
            io_error(error, "directory-open", "Cannot open storage directory component.");
            (void)close(fd);
            return false;
        }
        if (end == length)
            return own(next, fd, name, owner, private_directory, output, error);
        bool ok = trusted(next, owner, false, &status, error);
        (void)close(fd);
        if (!ok)
        {
            (void)close(next);
            return false;
        }
        fd = next;
        start = end + 1;
    }
    abort(); /* A validated non-root path always has a final component. */
}
bool tired_directory_child(TiredDirectory *parent, const char *name, bool create,
                           bool private_directory, TiredDirectory **output, TiredError *error)
{
    assert(parent != NULL && name != NULL && output != NULL && *output == NULL);
    if (!component(name, strnlen(name, 256), error) || !tired_directory_check(parent, error))
        return false;
    if (create && geteuid() != parent->owner)
        return tired_error_set(error, TIRED_AUTHORIZATION, "directory-create-owner",
                               "Directory creation must run as the storage owner.", 0);
    int parent_copy = fcntl(parent->fd, F_DUPFD_CLOEXEC, 3);
    if (parent_copy < 0)
        return io_error(error, "directory-duplicate", "Cannot retain storage parent descriptor.");
    bool created = false;
    if (create)
    {
        if (mkdirat(parent_copy, name, 0700) == 0)
            created = true;
        else if (errno != EEXIST)
        {
            io_error(error, "directory-create", "Cannot create private storage directory.");
            (void)close(parent_copy);
            return false;
        }
    }
    int fd =
        openat(parent_copy, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
    {
        io_error(error, "directory-open", "Cannot open storage child directory.");
        (void)close(parent_copy);
        return false;
    }
    TiredDirectory *directory = NULL;
    if (!own(fd, parent_copy, name, parent->owner, private_directory || created, &directory, error))
        return false;
    if (created && (fsync(fd) != 0 || fsync(parent_copy) != 0))
    {
        io_error(error, "directory-sync", "Cannot make storage directory creation durable.");
        tired_directory_destroy(directory);
        return false;
    }
    *output = directory;
    tired_error_clear(error);
    return true;
}
int tired_directory_fd(const TiredDirectory *directory)
{
    assert(directory != NULL);
    return directory->fd;
}
void tired_directory_destroy(TiredDirectory *directory)
{
    if (directory == NULL)
        return;
    (void)close(directory->fd);
    if (directory->parent >= 0)
        (void)close(directory->parent);
    free(directory);
}
