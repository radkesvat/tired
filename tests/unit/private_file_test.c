#include "tired/io.h"
#include "tired/private_file.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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
    int result = 1;
    char fixture[] = "private-file-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0}, contents = {0};
    TiredDirectory *directory = NULL;
    TiredError error = {0};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &directory, &error));
    int fd = tired_directory_fd(directory);
    const char data[] = {'a', '\0', 'b', '\n'};
    CHECK(tired_private_file_create(directory, "record", data, sizeof(data), &error));
    CHECK(tired_private_file_read(directory, "record", sizeof(data), &contents, &error));
    CHECK(contents.length == sizeof(data) && memcmp(contents.data, data, sizeof(data)) == 0);
    CHECK(!tired_private_file_create(directory, "record", "replacement", 11, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(!tired_private_file_read(directory, "record", 3, &contents, &error));
    CHECK(contents.length == sizeof(data) && memcmp(contents.data, data, sizeof(data)) == 0);
    CHECK(!tired_private_file_read(directory, "missing", 100, &contents, &error));
    CHECK(error.status == TIRED_NOT_FOUND);
    CHECK(tired_private_file_create(directory, "empty", NULL, 0, &error));
    CHECK(tired_private_file_read(directory, "empty", 0, &contents, &error));
    CHECK(contents.length == 0);
    CHECK(!tired_private_file_create(directory, "../escape", "x", 1, &error));
    CHECK(
        !tired_private_file_create(directory, "too-big", "", TIRED_PRIVATE_FILE_LIMIT + 1, &error));
    CHECK(!tired_private_file_read(directory, "record", TIRED_PRIVATE_FILE_LIMIT + 1, &contents,
                                   &error));
    CHECK(fchmodat(fd, "record", 0644, 0) == 0);
    CHECK(!tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(fchmodat(fd, "record", 0600, 0) == 0);
    CHECK(linkat(fd, "record", fd, "hard", 0) == 0);
    CHECK(!tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(!tired_private_file_read(directory, "hard", 100, &contents, &error));
    CHECK(unlinkat(fd, "hard", 0) == 0);
    CHECK(symlinkat("record", fd, "link") == 0);
    CHECK(!tired_private_file_read(directory, "link", 100, &contents, &error));
    CHECK(!tired_private_file_create(directory, "link", "x", 1, &error));
    CHECK(mkfifoat(fd, "fifo", 0600) == 0);
    CHECK(!tired_private_file_read(directory, "fifo", 100, &contents, &error));
    CHECK(!tired_private_file_create(directory, "fifo", "x", 1, &error));
    CHECK(mkdirat(fd, "subdir", 0700) == 0);
    CHECK(!tired_private_file_read(directory, "subdir", 100, &contents, &error));
    if (getuid() == 0)
    {
        CHECK(fchownat(fd, "record", 65534, (gid_t)-1, 0) == 0);
        CHECK(!tired_private_file_read(directory, "record", 100, &contents, &error));
        CHECK(fchownat(fd, "record", 0, (gid_t)-1, 0) == 0);
    }
    CHECK(fchmod(fd, 0755) == 0);
    CHECK(!tired_private_file_create(directory, "public", "x", 1, &error));
    CHECK(fchmod(fd, 0700) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        struct rlimit limit = {.rlim_cur = 2, .rlim_max = 2};
        if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limit) != 0)
            _exit(2);
        bool ok = tired_private_file_create(directory, "partial", "abcd", 4, &error);
        _exit(!ok && strcmp(error.code, "private-file-write") == 0 ? 0 : 3);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    struct stat partial;
    CHECK(fstatat(fd, "partial", &partial, 0) == 0 && partial.st_size == 2);
    CHECK(!tired_private_file_create(directory, "partial", "abcd", 4, &error));
    CHECK(tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(contents.length == sizeof(data) && memcmp(contents.data, data, sizeof(data)) == 0);
    result = 0;
cleanup:
    if (directory != NULL)
    {
        int cleanup_fd = tired_directory_fd(directory);
        (void)fchmod(cleanup_fd, 0700);
        const char *files[] = {"record", "empty", "hard", "link", "fifo", "partial", "public"};
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
            (void)unlinkat(cleanup_fd, files[i], 0);
        (void)unlinkat(cleanup_fd, "subdir", AT_REMOVEDIR);
    }
    tired_directory_destroy(directory);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    tired_text_destroy(&contents);
    free(cwd);
    return result;
}
