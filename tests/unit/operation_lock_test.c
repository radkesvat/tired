#include "tired/io.h"
#include "tired/operation_lock.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
int main(void)
{
    int result = 1, status, channel[2] = {-1, -1};
    pid_t child = -1;
    char fixture[] = "lock-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0};
    TiredDirectory *directory = NULL;
    TiredOperationLock *first = NULL, *second = NULL;
    TiredError error = {0};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &directory, &error));
    int fd = tired_directory_fd(directory);
    CHECK(tired_operation_lock_acquire(directory, &first, &error));
    CHECK(tired_operation_lock_check(first, &error));
    CHECK(!tired_operation_lock_acquire(directory, &second, &error));
    CHECK(strcmp(error.code, "operation-in-progress") == 0 && second == NULL);
    child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        if (tired_operation_lock_check(first, &error))
            _exit(1);
        tired_operation_lock_destroy(first);
        if (tired_operation_lock_acquire(directory, &second, &error) ||
            strcmp(error.code, "operation-in-progress") != 0)
            _exit(2);
        _exit(0);
    }
    CHECK(waitpid(child, &status, 0) == child);
    child = -1;
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(!tired_operation_lock_acquire(directory, &second, &error));
    CHECK(strcmp(error.code, "operation-in-progress") == 0);
    tired_operation_lock_destroy(first);
    first = NULL;
    CHECK(tired_operation_lock_acquire(directory, &first, &error));
    CHECK(renameat(fd, "operation.lock", fd, "old.lock") == 0);
    CHECK(tired_operation_lock_acquire(directory, &second, &error));
    CHECK(!tired_operation_lock_check(first, &error));
    CHECK(error.status == TIRED_CONFLICT);
    tired_operation_lock_destroy(first);
    first = NULL;
    CHECK(tired_operation_lock_check(second, &error));
    tired_operation_lock_destroy(second);
    second = NULL;
    CHECK(unlinkat(fd, "old.lock", 0) == 0);
    CHECK(fchmodat(fd, "operation.lock", 0644, 0) == 0);
    CHECK(!tired_operation_lock_acquire(directory, &first, &error));
    CHECK(fchmodat(fd, "operation.lock", 0600, 0) == 0);
    CHECK(linkat(fd, "operation.lock", fd, "extra.lock", 0) == 0);
    CHECK(!tired_operation_lock_acquire(directory, &first, &error));
    CHECK(unlinkat(fd, "extra.lock", 0) == 0 && unlinkat(fd, "operation.lock", 0) == 0);
    CHECK(symlinkat("missing", fd, "operation.lock") == 0);
    CHECK(!tired_operation_lock_acquire(directory, &first, &error));
    CHECK(unlinkat(fd, "operation.lock", 0) == 0);
    CHECK(mkfifoat(fd, "operation.lock", 0600) == 0);
    CHECK(!tired_operation_lock_acquire(directory, &first, &error));
    CHECK(unlinkat(fd, "operation.lock", 0) == 0);
    int unexpected = openat(fd, "operation.lock", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    CHECK(unexpected >= 0);
    ssize_t written = write(unexpected, "abc", 3);
    int closed = close(unexpected);
    CHECK(written == 3 && closed == 0);
    CHECK(!tired_operation_lock_acquire(directory, &first, &error));
    struct stat preserved;
    CHECK(fstatat(fd, "operation.lock", &preserved, 0) == 0 && preserved.st_size == 3);
    CHECK(unlinkat(fd, "operation.lock", 0) == 0);
    /* Process death releases the lock while its reusable pathname remains. */
    CHECK(pipe(channel) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        (void)close(channel[0]);
        if (!tired_operation_lock_acquire(directory, &first, &error))
            _exit(3);
        if (write(channel[1], "x", 1) != 1)
            _exit(4);
        for (;;)
            pause();
    }
    CHECK(close(channel[1]) == 0);
    channel[1] = -1;
    char ready;
    CHECK(read(channel[0], &ready, 1) == 1);
    CHECK(!tired_operation_lock_acquire(directory, &first, &error));
    CHECK(strcmp(error.code, "operation-in-progress") == 0);
    CHECK(kill(child, SIGKILL) == 0 && waitpid(child, &status, 0) == child);
    child = -1;
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(tired_operation_lock_acquire(directory, &first, &error));
    result = 0;
cleanup:
    if (child > 0)
    {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    for (unsigned i = 0; i < 2; ++i)
        if (channel[i] >= 0)
            (void)close(channel[i]);
    tired_operation_lock_destroy(second);
    tired_operation_lock_destroy(first);
    if (directory != NULL)
    {
        int cleanup_fd = tired_directory_fd(directory);
        (void)unlinkat(cleanup_fd, "operation.lock", 0);
        (void)unlinkat(cleanup_fd, "old.lock", 0);
        (void)unlinkat(cleanup_fd, "extra.lock", 0);
    }
    tired_directory_destroy(directory);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    free(cwd);
    return result;
}
