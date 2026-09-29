#include "tired/io.h"
#include "tired/private_file.h"
#include "tired/publication.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int fail_sync_fd = -1;
int __real_fsync(int fd);
int __wrap_fsync(int fd);
int __wrap_fsync(int fd)
{
    if (fail_sync_fd == fd || fail_sync_fd == -2)
    {
        fail_sync_fd = -1;
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
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
    char fixture[] = "publication-test-XXXXXX";
    char displaced[64] = {0};
    char staging_uuid[37] = {0};
    TiredFileFingerprint after = {0}, absent_before = {0};
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText path = {0}, contents = {0};
    TiredDirectory *directory = NULL;
    TiredOperationLock *lock = NULL;
    TiredPublication *publication = NULL;
    TiredError error = {0};
    CHECK(cwd != NULL && (created = mkdtemp(fixture)) != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &path, &error));
    CHECK(tired_directory_open(path.data, getuid(), true, &directory, &error));
    CHECK(tired_operation_lock_acquire(directory, &lock, &error));
    int fd = tired_directory_fd(directory);
    struct stat status;
    CHECK(
        tired_publication_prepare(directory, "record", "complete", 8, 0600, &publication, &error));
    CHECK(!tired_publication_published(publication) && !tired_publication_durable(publication));
    CHECK(fstatat(fd, "record", &status, 0) < 0);
    CHECK(tired_private_file_read(directory, tired_publication_temporary_name(publication), 100,
                                  &contents, &error));
    CHECK(strcmp(contents.data, "complete") == 0);
    memcpy(staging_uuid, tired_publication_temporary_name(publication) + 7, 36);
    CHECK(tired_file_fingerprint(directory, tired_publication_temporary_name(publication), 100,
                                 &after, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_reopen(directory, "record", staging_uuid, &absent_before, &after, lock,
                                   &publication, &error));
    CHECK(!tired_publication_published(publication));
    fail_sync_fd = fd;
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(tired_publication_published(publication) && !tired_publication_durable(publication));
    CHECK(strcmp(error.code, "publication-sync") == 0);
    CHECK(tired_publication_commit(publication, lock, &error));
    CHECK(tired_publication_published(publication) && tired_publication_durable(publication));
    CHECK(fstatat(fd, tired_publication_temporary_name(publication), &status, 0) < 0);
    CHECK(tired_publication_commit(publication, lock, &error));
    CHECK(tired_publication_discard(publication, &error));
    CHECK(tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(strcmp(contents.data, "complete") == 0);
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_reopen(directory, "record", staging_uuid, &absent_before, &after, lock,
                                   &publication, &error));
    CHECK(tired_publication_published(publication) && !tired_publication_durable(publication));
    CHECK(tired_publication_commit(publication, lock, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_prepare(directory, "record", "other", 5, 0600, &publication, &error));
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(error.status == TIRED_CONFLICT && !tired_publication_published(publication));
    CHECK(tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(strcmp(contents.data, "complete") == 0);
    CHECK(tired_publication_discard(publication, &error));
    CHECK(tired_publication_discard(publication, &error));
    CHECK(!tired_publication_commit(publication, lock, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_prepare(directory, "unit.service", "[Unit]\n", 7, 0644, &publication,
                                    &error));
    CHECK(symlinkat("missing", fd, "unit.service") == 0);
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(error.status == TIRED_CONFLICT && !tired_publication_published(publication));
    CHECK(fstatat(fd, "unit.service", &status, AT_SYMLINK_NOFOLLOW) == 0 &&
          S_ISLNK(status.st_mode));
    CHECK(unlinkat(fd, "unit.service", 0) == 0);
    CHECK(tired_publication_commit(publication, lock, &error));
    CHECK(fstatat(fd, "unit.service", &status, 0) == 0 && (status.st_mode & 07777) == 0644);
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(
        tired_publication_prepare(directory, "changed", "original", 8, 0600, &publication, &error));
    const char *temporary = tired_publication_temporary_name(publication);
    CHECK(renameat(fd, temporary, fd, "saved") == 0);
    CHECK(tired_private_file_create(directory, temporary, "foreign", 7, &error));
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(!tired_publication_discard(publication, &error));
    CHECK(tired_private_file_read(directory, temporary, 100, &contents, &error));
    CHECK(strcmp(contents.data, "foreign") == 0);
    CHECK(unlinkat(fd, temporary, 0) == 0 && renameat(fd, "saved", fd, temporary) == 0);
    CHECK(tired_publication_discard(publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_prepare(directory, "modified", "original", 8, 0600, &publication,
                                    &error));
    int writer =
        openat(fd, tired_publication_temporary_name(publication), O_WRONLY | O_APPEND | O_CLOEXEC);
    CHECK(writer >= 0);
    ssize_t written = write(writer, "x", 1);
    int closed = close(writer);
    CHECK(written == 1 && closed == 0);
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(strcmp(error.code, "publication-modified") == 0);
    CHECK(tired_publication_discard(publication, &error));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(!tired_publication_prepare(directory, "../escape", "", 0, 0600, &publication, &error));
    CHECK(publication == NULL);
    fail_sync_fd = -2;
    CHECK(!tired_publication_prepare(directory, "prepare-failed", "data", 4, 0600, &publication,
                                     &error));
    CHECK(publication != NULL && !tired_publication_published(publication));
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(tired_publication_discard(publication, &error));
    CHECK(fstatat(fd, "prepare-failed", &status, 0) < 0);
    tired_publication_destroy(publication);
    publication = NULL;
    TiredFileFingerprint before = {0}, mismatch = {0};
    CHECK(tired_file_fingerprint(directory, "record", 100, &before, &error));
    CHECK(tired_publication_prepare(directory, "record", "replacement", 11, 0600, &publication,
                                    &error));
    memcpy(staging_uuid, tired_publication_temporary_name(publication) + 7, 36);
    CHECK(tired_file_fingerprint(directory, tired_publication_temporary_name(publication), 100,
                                 &after, &error));
    mismatch = before;
    ++mismatch.inode;
    CHECK(!tired_publication_replace(publication, lock, &mismatch, &error));
    CHECK(!tired_publication_published(publication));
    fail_sync_fd = fd;
    CHECK(!tired_publication_replace(publication, lock, &before, &error));
    CHECK(tired_publication_published(publication) && !tired_publication_durable(publication));
    (void)snprintf(displaced, sizeof(displaced), "%s",
                   tired_publication_temporary_name(publication));
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(tired_publication_reopen(directory, "record", staging_uuid, &before, &after, lock,
                                   &publication, &error));
    CHECK(tired_publication_published(publication) && !tired_publication_durable(publication));
    CHECK(tired_publication_replace(publication, lock, &before, &error));
    CHECK(tired_publication_durable(publication));
    CHECK(tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(strcmp(contents.data, "replacement") == 0);
    CHECK(tired_private_file_read(directory, displaced, 100, &contents, &error));
    CHECK(strcmp(contents.data, "complete") == 0);
    CHECK(tired_publication_commit(publication, lock, &error));
    CHECK(!tired_publication_replace(publication, lock, &mismatch, &error));
    CHECK(tired_publication_discard(publication, &error));
    CHECK(fstatat(fd, displaced, &status, 0) == 0);
    CHECK(fchmodat(fd, displaced, 0644, 0) == 0);
    CHECK(!tired_publication_commit(publication, lock, &error));
    CHECK(strcmp(error.code, "replacement-displaced") == 0);
    CHECK(tired_private_file_read(directory, "record", 100, &contents, &error));
    CHECK(strcmp(contents.data, "replacement") == 0);
    tired_publication_destroy(publication);
    publication = NULL;
    CHECK(!tired_publication_reopen(directory, "record", staging_uuid, &before, &after, lock,
                                    &publication, &error));
    CHECK(publication == NULL && error.status == TIRED_CONFLICT);
    result = 0;
cleanup:
    fail_sync_fd = -1;
    if (publication != NULL)
        (void)tired_publication_discard(publication, &error);
    tired_publication_destroy(publication);
    tired_operation_lock_destroy(lock);
    if (directory != NULL)
    {
        int cleanup_fd = tired_directory_fd(directory);
        if (displaced[0] != '\0')
            (void)unlinkat(cleanup_fd, displaced, 0);
        const char *files[] = {"record",  "unit.service", "saved",
                               "changed", "modified",     "operation.lock"};
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
            (void)unlinkat(cleanup_fd, files[i], 0);
    }
    tired_directory_destroy(directory);
    if (created != NULL)
        (void)rmdir(created);
    tired_text_destroy(&path);
    tired_text_destroy(&contents);
    free(cwd);
    return result;
}
