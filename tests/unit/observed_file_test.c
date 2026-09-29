#include "tired/io.h"
#include "tired/observed_file.h"
#include "tired/private_file.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(void)
{
    int result = 1;
    char fixture[] = "observed-XXXXXX";
    char *created = NULL, *cwd = NULL;
    TiredText path = {0}, target = {0}, input = {0}, expected = {0};
    TiredDirectory *root = NULL, *actual = NULL, *unsafe = NULL, *deep = NULL;
    TiredObservedFile file = {0};
    TiredError error = {0};
    size_t budget = TIRED_INPUT_LIMIT;
    cwd = getcwd(NULL, 0);
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, geteuid(), true, &root, &error));
    CHECK(tired_directory_child(root, "actual", true, true, &actual, &error));
    CHECK(tired_directory_child(root, "unsafe", true, true, &unsafe, &error));
    CHECK(tired_directory_child(actual, "deep", true, true, &deep, &error));
    CHECK(tired_private_file_create(actual, "unit.conf", "abc", 3, &error));
    CHECK(tired_path_absolute(&path, "actual", 6, &target, &error));
    CHECK(tired_path_absolute(&target, "unit.conf", 9, &expected, &error));
    int fd = tired_directory_fd(root);
    CHECK(symlinkat("actual", fd, "relative") == 0);
    CHECK(symlinkat(target.data, fd, "absolute") == 0);
    CHECK(symlinkat("relative", fd, "chain") == 0);
    CHECK(symlinkat("actual/../actual", fd, "backref") == 0);
    CHECK(symlinkat("actual/deep", fd, "inner") == 0);
    CHECK(symlinkat("inner/..", fd, "through") == 0);
    CHECK(symlinkat("unsafe/../actual", fd, "untrusted") == 0);
    CHECK(symlinkat("cycle-b", fd, "cycle-a") == 0 && symlinkat("cycle-a", fd, "cycle-b") == 0);
    CHECK(symlinkat("actual/unit.conf", fd, "leaf") == 0);
    const char *good[] = {"actual/unit.conf", "relative/unit.conf", "absolute/unit.conf",
                          "chain/unit.conf",  "backref/unit.conf",  "through/unit.conf"};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
    {
        CHECK(tired_path_absolute(&path, good[i], strlen(good[i]), &input, &error));
        budget = 3;
        CHECK(tired_observed_file_read(input.data, true, &budget, &file, &error));
        CHECK(budget == 0 && file.fingerprint.exists && file.bytes.length == 3 &&
              memcmp(file.bytes.data, "abc", 3) == 0 &&
              strcmp(file.resolved_path.data, expected.data) == 0);
    }
    char *saved = file.bytes.data;
    CHECK(!tired_observed_file_read(input.data, true, &budget, &file, &error));
    CHECK(error.status == TIRED_RECOVERY_REQUIRED && file.bytes.data == saved);
    CHECK(fchmod(tired_directory_fd(unsafe), 0777) == 0);
    const char *bad[] = {"untrusted/unit.conf", "cycle-a/unit.conf", "leaf",
                         "actual/unit.conf/child"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(tired_path_absolute(&path, bad[i], strlen(bad[i]), &input, &error));
        budget = TIRED_INPUT_LIMIT;
        CHECK(!tired_observed_file_read(input.data, true, &budget, &file, &error));
        CHECK(error.status == TIRED_CONFLICT && file.bytes.data == saved);
    }
    CHECK(!tired_observed_file_read("relative/path", true, &budget, &file, &error));
    CHECK(!tired_observed_file_read("/", true, &budget, &file, &error));
    CHECK(file.bytes.data == saved);
    char magic[128];
    (void)snprintf(magic, sizeof(magic), "/proc/self/fd/%d/unit.conf", tired_directory_fd(actual));
    CHECK(!tired_observed_file_read(magic, true, &budget, &file, &error));
    CHECK(error.status == TIRED_CONFLICT && file.bytes.data == saved);
    CHECK(tired_path_absolute(&path, "actual/missing.conf", 19, &input, &error));
    budget = TIRED_INPUT_LIMIT;
    CHECK(tired_observed_file_read(input.data, true, &budget, &file, &error));
    CHECK(!file.fingerprint.exists && file.bytes.length == 0 && budget == TIRED_INPUT_LIMIT);
    if (geteuid() == 0)
    {
        CHECK(fchownat(fd, "relative", 1, 1, AT_SYMLINK_NOFOLLOW) == 0);
        CHECK(tired_path_absolute(&path, "relative/unit.conf", 18, &input, &error));
        CHECK(!tired_observed_file_read(input.data, false, &budget, &file, &error));
        CHECK(error.status == TIRED_CONFLICT);
    }
    result = 0;
cleanup:
    if (actual != NULL)
    {
        (void)unlinkat(tired_directory_fd(actual), "unit.conf", 0);
        (void)unlinkat(tired_directory_fd(actual), "deep", AT_REMOVEDIR);
    }
    tired_directory_destroy(deep);
    tired_directory_destroy(actual);
    tired_directory_destroy(unsafe);
    if (root != NULL)
    {
        const char *links[] = {"relative", "absolute", "chain", "backref", "untrusted",
                               "cycle-a",  "cycle-b",  "leaf",  "inner",   "through"};
        for (size_t i = 0; i < sizeof(links) / sizeof(links[0]); ++i)
            (void)unlinkat(tired_directory_fd(root), links[i], 0);
        (void)unlinkat(tired_directory_fd(root), "actual", AT_REMOVEDIR);
        (void)unlinkat(tired_directory_fd(root), "unsafe", AT_REMOVEDIR);
    }
    tired_directory_destroy(root);
    if (created != NULL)
        (void)rmdir(created);
    free(cwd);
    tired_text_destroy(&path);
    tired_text_destroy(&target);
    tired_text_destroy(&input);
    tired_text_destroy(&expected);
    tired_observed_file_destroy(&file);
    return result;
}
