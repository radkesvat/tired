#include "tired/directory.h"
#include "tired/io.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    int result = 1, fixture_fd = -1;
    char fixture[] = "directory-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0}, invalid = {0};
    TiredDirectory *root = NULL, *child = NULL, *second = NULL;
    TiredError error = {0};
    CHECK(cwd != NULL);
    created = mkdtemp(fixture);
    CHECK(created != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    fixture_fd = open(path.data, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(fixture_fd >= 0);
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(tired_directory_check(root, &error));
    CHECK((fcntl(tired_directory_fd(root), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK(!tired_directory_child(root, "absent", false, true, &child, &error));
    CHECK(error.status == TIRED_NOT_FOUND && child == NULL);
    struct stat status;
    CHECK(fstatat(fixture_fd, "absent", &status, AT_SYMLINK_NOFOLLOW) < 0);
    const char *bad[] = {"", ".", "..", "../other", "/absolute", "a/b", "bad\nname"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(!tired_directory_child(root, bad[i], true, true, &child, &error));
        CHECK(error.status == TIRED_INVALID && child == NULL);
    }
    CHECK(tired_directory_child(root, "private", true, true, &child, &error));
    CHECK(fstat(tired_directory_fd(child), &status) == 0 && (status.st_mode & 07777) == 0700);
    CHECK(status.st_uid == getuid());
    CHECK(tired_directory_child(root, "private", true, true, &second, &error));
    tired_directory_destroy(second);
    second = NULL;
    /* Each child retains its own parent descriptor. */
    tired_directory_destroy(root);
    root = NULL;
    CHECK(tired_directory_check(child, &error));
    CHECK(renameat(fixture_fd, "private", fixture_fd, "moved") == 0);
    CHECK(mkdirat(fixture_fd, "private", 0700) == 0);
    CHECK(!tired_directory_check(child, &error));
    CHECK(error.status == TIRED_CONFLICT && strcmp(error.code, "directory-replaced") == 0);
    CHECK(!tired_directory_child(child, "must-not-exist", true, true, &second, &error));
    CHECK(fstatat(tired_directory_fd(child), "must-not-exist", &status, AT_SYMLINK_NOFOLLOW) < 0);
    tired_directory_destroy(child);
    child = NULL;
    CHECK(unlinkat(fixture_fd, "private", AT_REMOVEDIR) == 0);
    CHECK(renameat(fixture_fd, "moved", fixture_fd, "private") == 0);
    CHECK(tired_directory_open(path.data, getuid(), true, &root, &error));
    CHECK(fchmodat(fixture_fd, "private", 0755, 0) == 0);
    CHECK(!tired_directory_child(root, "private", true, true, &child, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(fstatat(fixture_fd, "private", &status, 0) == 0 && (status.st_mode & 07777) == 0755);
    CHECK(tired_directory_child(root, "private", false, false, &child, &error));
    CHECK(fchmod(tired_directory_fd(child), 0777) == 0);
    CHECK(!tired_directory_check(child, &error));
    CHECK(fchmod(tired_directory_fd(child), 0700) == 0);
    CHECK(tired_directory_check(child, &error));
    if (getuid() == 0)
    {
        CHECK(fchown(tired_directory_fd(child), 65534, (gid_t)-1) == 0);
        CHECK(!tired_directory_check(child, &error));
        CHECK(fchown(tired_directory_fd(child), 0, (gid_t)-1) == 0);
    }
    tired_directory_destroy(child);
    child = NULL;
    CHECK(symlinkat("private", fixture_fd, "link") == 0);
    CHECK(!tired_directory_child(root, "link", false, true, &child, &error));
    CHECK(tired_path_absolute(&path, "link/missing", 12, &invalid, &error));
    CHECK(!tired_directory_open(invalid.data, getuid(), true, &child, &error));
    CHECK(error.status != TIRED_NOT_FOUND);
    CHECK(unlinkat(fixture_fd, "link", 0) == 0);
    CHECK(symlinkat("missing", fixture_fd, "link") == 0);
    CHECK(!tired_directory_child(root, "link", true, true, &child, &error));
    CHECK(unlinkat(fixture_fd, "link", 0) == 0);
    CHECK(mkfifoat(fixture_fd, "fifo", 0600) == 0);
    CHECK(!tired_directory_child(root, "fifo", true, true, &child, &error));
    CHECK(unlinkat(fixture_fd, "fifo", 0) == 0);
    CHECK(tired_path_absolute(&path, "missing/../invalid", 18, &invalid, &error));
    CHECK(!tired_directory_open(invalid.data, getuid(), true, &child, &error));
    CHECK(error.status == TIRED_INVALID);
    CHECK(fchmod(fixture_fd, 0777) == 0);
    CHECK(!tired_directory_open(path.data, getuid(), true, &child, &error));
    CHECK(!tired_directory_child(root, "blocked", true, true, &child, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(fchmod(fixture_fd, 0700) == 0);
    result = 0;
cleanup:
    tired_directory_destroy(second);
    tired_directory_destroy(child);
    tired_directory_destroy(root);
    if (fixture_fd >= 0)
    {
        (void)fchmod(fixture_fd, 0700);
        (void)unlinkat(fixture_fd, "link", 0);
        (void)unlinkat(fixture_fd, "fifo", 0);
        (void)unlinkat(fixture_fd, "private", AT_REMOVEDIR);
        (void)unlinkat(fixture_fd, "moved", AT_REMOVEDIR);
        (void)close(fixture_fd);
    }
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    tired_text_destroy(&invalid);
    free(cwd);
    return result;
}
