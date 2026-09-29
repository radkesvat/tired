#include "tired/private_file.h"
#include "tired/capture.h"
#include "tired/io.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool io_error(TiredError *error, const char *code, const char *message)
{
    int number = errno;
    TiredStatus status = number == ENOENT                      ? TIRED_NOT_FOUND
                         : number == EEXIST                    ? TIRED_CONFLICT
                         : number == EACCES || number == EPERM ? TIRED_AUTHORIZATION
                                                               : TIRED_RUNTIME_FAILED;
    return tired_error_set(error, status, code, message, number);
}
static bool inputs(TiredDirectory *directory, const char *name, size_t limit, TiredError *error)
{
    size_t length = strnlen(name, 256);
    if (length == 0 || length > 255 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        memchr(name, '/', length) != NULL || !tired_validate_text(name, length, true, error) ||
        limit > TIRED_PRIVATE_FILE_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "private-file-input",
                               "Private file name or byte limit is invalid.", 0);
    if (!tired_directory_check(directory, error))
        return false;
    struct stat parent;
    if (fstat(tired_directory_fd(directory), &parent) != 0)
        return io_error(error, "private-file-directory", "Cannot inspect private file directory.");
    if (parent.st_uid != geteuid() || (parent.st_mode & 07777) != 0700)
        return tired_error_set(error, TIRED_CONFLICT, "private-file-directory",
                               "Private files require an owner directory with mode 0700.", 0);
    return true;
}
static bool trusted(const struct stat *file, TiredError *error)
{
    if (!S_ISREG(file->st_mode) || file->st_uid != geteuid() || file->st_nlink != 1 ||
        (file->st_mode & 07777) != 0600)
        return tired_error_set(error, TIRED_CONFLICT, "private-file-trust",
                               "Private file has unsafe type, owner, permissions or hard links.",
                               0);
    return true;
}
static bool binding(TiredDirectory *directory, const char *name, int fd, TiredError *error)
{
    struct stat file, entry;
    if (!inputs(directory, name, 0, error))
        return false;
    if (fstat(fd, &file) != 0 ||
        fstatat(tired_directory_fd(directory), name, &entry, AT_SYMLINK_NOFOLLOW) != 0)
        return io_error(error, "private-file-stat", "Cannot inspect private file binding.");
    if (!trusted(&file, error) || !trusted(&entry, error))
        return false;
    if (file.st_dev != entry.st_dev || file.st_ino != entry.st_ino)
        return tired_error_set(error, TIRED_CONFLICT, "private-file-replaced",
                               "Private file changed during the operation.", 0);
    return true;
}
bool tired_private_file_read(TiredDirectory *directory, const char *name, size_t limit,
                             TiredText *output, TiredError *error)
{
    assert(directory != NULL && name != NULL && output != NULL);
    if (!inputs(directory, name, limit, error))
        return false;
    struct stat initial;
    if (fstatat(tired_directory_fd(directory), name, &initial, AT_SYMLINK_NOFOLLOW) != 0)
        return io_error(error, "private-file-stat", "Cannot inspect requested private file.");
    if (!trusted(&initial, error))
        return false;
    int fd = openat(tired_directory_fd(directory), name,
                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        return io_error(error, "private-file-open", "Cannot open requested private file.");
    TiredText contents = {0};
    bool ok = binding(directory, name, fd, error) && tired_read_fd(fd, limit, &contents, error) &&
              binding(directory, name, fd, error);
    if (close(fd) != 0 && ok)
        ok = io_error(error, "private-file-close", "Cannot close requested private file.");
    if (ok)
    {
        tired_text_destroy(output);
        *output = contents;
        contents = (TiredText){0};
        tired_error_clear(error);
    }
    tired_text_destroy(&contents);
    return ok;
}
bool tired_private_file_create(TiredDirectory *directory, const char *name, const char *data,
                               size_t length, TiredError *error)
{
    assert(directory != NULL && name != NULL && (data != NULL || length == 0));
    if (!inputs(directory, name, length, error))
        return false;
    int parent = tired_directory_fd(directory);
    int fd =
        openat(parent, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, 0600);
    if (fd < 0)
        return io_error(error, "private-file-create", "Cannot exclusively create private file.");
    bool ok = binding(directory, name, fd, error);
    size_t written = 0;
    while (ok && written < length)
    {
        size_t count = length - written;
        if (count > 65536)
            count = 65536;
        ssize_t bytes = write(fd, data + written, count);
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0)
        {
            if (bytes == 0)
                errno = EIO;
            ok = io_error(error, "private-file-write",
                          "Private file write failed; partial data may remain.");
        }
        else
            written += (size_t)bytes;
    }
    if (ok && fsync(fd) != 0)
        ok = io_error(error, "private-file-sync",
                      "Private file sync failed; durability is unknown.");
    if (ok)
        ok = binding(directory, name, fd, error);
    if (close(fd) != 0 && ok)
        ok = io_error(error, "private-file-close",
                      "Private file close failed; completion is unknown.");
    if (ok && fsync(parent) != 0)
        ok = io_error(error, "private-file-directory-sync",
                      "Directory sync failed; durability is unknown.");
    if (ok)
        tired_error_clear(error);
    return ok;
}
