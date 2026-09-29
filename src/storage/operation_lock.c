#include "tired/operation_lock.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

struct TiredOperationLock
{
    TiredDirectory *runtime;
    int fd;
    pid_t process;
};
static bool io_error(TiredError *error)
{
    int number = errno;
    return tired_error_set(
        error, number == EACCES || number == EPERM ? TIRED_AUTHORIZATION : TIRED_RUNTIME_FAILED,
        "operation-lock-io", "Cannot access the operation lock.", number);
}
static bool directory_check(TiredDirectory *runtime, TiredError *error)
{
    if (!tired_directory_check(runtime, error))
        return false;
    struct stat status;
    if (fstat(tired_directory_fd(runtime), &status) != 0)
        return io_error(error);
    if (status.st_uid != geteuid() || (status.st_mode & 07777) != 0700)
        return tired_error_set(
            error, TIRED_CONFLICT, "operation-lock-directory",
            "Operation lock requires a private directory owned by this identity.", 0);
    return true;
}
static bool file_check(const struct stat *status, TiredError *error)
{
    if (!S_ISREG(status->st_mode) || status->st_uid != geteuid() ||
        (status->st_mode & 07777) != 0600 || status->st_nlink != 1 || status->st_size != 0)
        return tired_error_set(
            error, TIRED_CONFLICT, "operation-lock-file",
            "Operation lock must be an empty private regular file with one link.", 0);
    return true;
}
bool tired_operation_lock_check(const TiredOperationLock *lock, TiredError *error)
{
    assert(lock != NULL);
    if (lock->process != getpid())
        return tired_error_set(
            error, TIRED_CONFLICT, "operation-lock-process",
            "An inherited lock cannot authorize another process to mutate state.", 0);
    if (!directory_check(lock->runtime, error))
        return false;
    struct stat file, binding;
    if (fstat(lock->fd, &file) != 0 || fstatat(tired_directory_fd(lock->runtime), "operation.lock",
                                               &binding, AT_SYMLINK_NOFOLLOW) != 0)
        return io_error(error);
    if (!file_check(&file, error) || !file_check(&binding, error))
        return false;
    if (file.st_dev != binding.st_dev || file.st_ino != binding.st_ino)
        return tired_error_set(error, TIRED_CONFLICT, "operation-lock-replaced",
                               "Operation lock was replaced; mutation refused.", 0);
    tired_error_clear(error);
    return true;
}
bool tired_operation_lock_acquire(TiredDirectory *runtime, TiredOperationLock **output,
                                  TiredError *error)
{
    assert(runtime != NULL && output != NULL && *output == NULL);
    if (!directory_check(runtime, error))
        return false;
    TiredOperationLock *lock = calloc(1, sizeof(*lock));
    if (lock == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate operation lock.", errno);
    lock->runtime = runtime;
    lock->process = getpid();
    int directory = tired_directory_fd(runtime);
    int flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY;
    lock->fd = openat(directory, "operation.lock", flags | O_CREAT | O_EXCL, 0600);
    bool created = lock->fd >= 0;
    if (lock->fd < 0 && errno == EEXIST)
    {
        struct stat existing;
        if (fstatat(directory, "operation.lock", &existing, AT_SYMLINK_NOFOLLOW) != 0)
        {
            io_error(error);
            goto failed;
        }
        if (!file_check(&existing, error))
            goto failed;
        lock->fd = openat(directory, "operation.lock", flags);
    }
    if (lock->fd < 0)
    {
        io_error(error);
        goto failed;
    }
    if (!tired_operation_lock_check(lock, error))
        goto failed;
    if (flock(lock->fd, LOCK_EX | LOCK_NB) != 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            tired_error_set(error, TIRED_CONFLICT, "operation-in-progress",
                            "Another tired operation is in progress for this scope.", 0);
        else
            io_error(error);
        goto failed;
    }
    if (!tired_operation_lock_check(lock, error))
        goto failed;
    if (created && (fsync(lock->fd) != 0 || fsync(directory) != 0))
    {
        io_error(error);
        goto failed;
    }
    *output = lock;
    tired_error_clear(error);
    return true;
failed:
    tired_operation_lock_destroy(lock);
    return false;
}
void tired_operation_lock_destroy(TiredOperationLock *lock)
{
    if (lock == NULL)
        return;
    if (lock->fd >= 0)
        (void)close(lock->fd);
    free(lock);
}
