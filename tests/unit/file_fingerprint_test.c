#include "tired/file_fingerprint.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int mutation, parent_fd = -1;
static dev_t watched_device;
static ino_t watched_inode;
ssize_t __real_read(int fd, void *buffer, size_t count);
ssize_t __wrap_read(int fd, void *buffer, size_t count);
ssize_t __wrap_read(int fd, void *buffer, size_t count)
{
    ssize_t result = __real_read(fd, buffer, count);
    struct stat status;
    if (result > 0 && mutation != 0 && fstat(fd, &status) == 0 && status.st_dev == watched_device &&
        status.st_ino == watched_inode)
    {
        int action = mutation;
        mutation = 0;
        if (action == 1 && renameat(parent_fd, "unit", parent_fd, "saved") != 0)
            return -1;
        int writer =
            openat(parent_fd, "unit",
                   O_WRONLY | O_CLOEXEC | (action == 1 ? O_CREAT | O_EXCL : O_APPEND), 0600);
        if (writer < 0)
            return -1;
        ssize_t written = write(writer, action == 1 ? "abc" : "x", action == 1 ? 3 : 1);
        int closed = close(writer);
        if (written != (action == 1 ? 3 : 1) || closed != 0)
            return -1;
    }
    return result;
}
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
    char fixture[] = "fingerprint-test-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0}, encoded = {0}, loaded = {0};
    TiredDirectory *directory = NULL;
    TiredFileFingerprint snapshot = {0}, original = {0};
    TiredError error = {0};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &directory, &error));
    parent_fd = tired_directory_fd(directory);
    CHECK(tired_file_fingerprint(directory, "unit", 100, &snapshot, &error) && !snapshot.exists);
    CHECK(tired_private_file_create(directory, "unit", "abc", 3, &error));
    CHECK(tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(snapshot.exists && snapshot.size == 3 && snapshot.mode == 0600 &&
          snapshot.uid == getuid());
    CHECK(strcmp(snapshot.sha256,
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    original = snapshot;
    CHECK(tired_file_fingerprint_encode(&snapshot, &encoded, &error));
    CHECK(tired_private_file_create(directory, "fingerprint.json", encoded.data, encoded.length,
                                    &error));
    CHECK(tired_private_file_read(directory, "fingerprint.json", 4096, &loaded, &error));
    TiredFileFingerprint persisted = {0};
    CHECK(tired_file_fingerprint_parse(loaded.data, loaded.length, &persisted, &error));
    CHECK(tired_file_fingerprint_equal(&snapshot, &persisted));
    CHECK(!tired_file_fingerprint(directory, "unit", 2, &snapshot, &error));
    CHECK(tired_file_fingerprint_equal(&snapshot, &original));
    CHECK(utimensat(parent_fd, "unit", NULL, 0) == 0);
    CHECK(tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(tired_file_fingerprint_equal(&snapshot, &original));
    CHECK(fchmodat(parent_fd, "unit", 0644, 0) == 0);
    CHECK(tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(!tired_file_fingerprint_equal(&snapshot, &original));
    CHECK(fchmodat(parent_fd, "unit", 0600, 0) == 0);
    CHECK(tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(tired_file_fingerprint_equal(&snapshot, &original));
    CHECK(linkat(parent_fd, "unit", parent_fd, "hard", 0) == 0);
    CHECK(!tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(unlinkat(parent_fd, "hard", 0) == 0);
    CHECK(symlinkat("unit", parent_fd, "link") == 0);
    CHECK(!tired_file_fingerprint(directory, "link", 3, &snapshot, &error));
    CHECK(mkfifoat(parent_fd, "fifo", 0600) == 0);
    CHECK(!tired_file_fingerprint(directory, "fifo", 3, &snapshot, &error));
    CHECK(!tired_file_fingerprint(directory, "../unit", 3, &snapshot, &error));
    CHECK(tired_file_fingerprint_equal(&snapshot, &original));
    CHECK(tired_private_file_create(directory, "empty", NULL, 0, &error));
    CHECK(tired_file_fingerprint(directory, "empty", 0, &snapshot, &error));
    CHECK(snapshot.exists && snapshot.size == 0 &&
          strcmp(snapshot.sha256,
                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    snapshot = original;
    watched_device = original.device;
    watched_inode = original.inode;
    mutation = 1;
    CHECK(!tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(mutation == 0 && error.status == TIRED_CONFLICT);
    CHECK(tired_file_fingerprint_equal(&snapshot, &original));
    CHECK(tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(strcmp(snapshot.sha256, original.sha256) == 0 &&
          !tired_file_fingerprint_equal(&snapshot, &original));
    original = snapshot;
    watched_inode = snapshot.inode;
    mutation = 2;
    CHECK(!tired_file_fingerprint(directory, "unit", 3, &snapshot, &error));
    CHECK(mutation == 0 && strcmp(error.code, "fingerprint-limit") == 0);
    CHECK(tired_file_fingerprint_equal(&snapshot, &original));
    result = 0;
cleanup:
    mutation = 0;
    if (directory != NULL)
    {
        const char *names[] = {"unit",  "saved",           "hard", "link", "fifo",
                               "empty", "fingerprint.json"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            (void)unlinkat(tired_directory_fd(directory), names[i], 0);
    }
    tired_directory_destroy(directory);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    tired_text_destroy(&encoded);
    tired_text_destroy(&loaded);
    free(cwd);
    return result;
}
