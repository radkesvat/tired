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
    int result = 1, original = -1;
    char fixture[] = "/tmp/tired-io-XXXXXX";
    char *root = NULL;
    TiredText data = {0}, directory = {0}, path = {0};
    TiredError error = {0};
    char uuid[37], next[37];
    CHECK(tired_uuid_create(uuid, &error));
    CHECK(tired_uuid_create(next, &error));
    CHECK(strlen(uuid) == 36 && uuid[14] == '4');
    CHECK(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b');
    CHECK(strcmp(uuid, next) != 0);
    original = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(original >= 0);
    root = mkdtemp(fixture);
    CHECK(root != NULL);
    CHECK(chdir(root) == 0);
    CHECK(tired_text_set(&directory, root, strlen(root), 4096, &error));
    CHECK(tired_path_absolute(&directory, "current/../file", 15, &path, &error));
    CHECK(strstr(path.data, "/current/../file") != NULL);
    CHECK(tired_write_private_new("output", "a\0bc", 4, &error));
    struct stat status;
    CHECK(stat("output", &status) == 0 && (status.st_mode & 0777) == 0600);
    CHECK(!tired_write_private_new("output", "replace", 7, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(tired_read_file("output", 4, &data, &error));
    CHECK(data.length == 4 && memcmp(data.data, "a\0bc", 4) == 0);
    CHECK(!tired_read_file("output", 3, &data, &error));
    CHECK(data.length == 4);
    CHECK(symlink("output", "link") == 0);
    CHECK(!tired_write_private_new("link", "replace", 7, &error));
    CHECK(tired_read_file("link", 4, &data, &error));
    CHECK(mkfifo("fifo", 0600) == 0);
    CHECK(!tired_read_file("fifo", 1024, &data, &error));
    CHECK(!tired_read_file(".", 1024, &data, &error));
    CHECK(tired_write_private_new("empty", NULL, 0, &error));
    CHECK(tired_read_file("empty", 0, &data, &error));
    CHECK(data.length == 0 && data.data != NULL);
    CHECK(!tired_read_file("missing", 1024, &data, &error));
    result = 0;
cleanup:
    tired_text_destroy(&data);
    tired_text_destroy(&directory);
    tired_text_destroy(&path);
    if (root != NULL && chdir(root) == 0)
    {
        (void)unlink("output");
        (void)unlink("link");
        (void)unlink("fifo");
        (void)unlink("empty");
    }
    if (original >= 0)
    {
        if (fchdir(original) != 0)
            result = 1;
        (void)close(original);
    }
    if (root != NULL && rmdir(root) != 0)
    {
        fprintf(stderr, "Fixture cleanup failed: %s\n", root);
        result = 1;
    }
    return result;
}
