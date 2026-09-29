#define _GNU_SOURCE
#include "tired/publication.h"
#include "tired/capture.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct TiredPublication
{
    TiredDirectory *directory;
    int fd;
    char temporary[64], name[256];
    struct stat prepared_identity;
    bool staged, ready, published, durable;
    bool exchanged;
    TiredFileFingerprint previous;
    unsigned mode;
};
static bool io_error(TiredError *error, const char *code, const char *message)
{
    int number = errno;
    TiredStatus status = number == EEXIST                           ? TIRED_CONFLICT
                         : number == EACCES || number == EPERM      ? TIRED_AUTHORIZATION
                         : number == ENOSYS || number == EOPNOTSUPP ? TIRED_UNSUPPORTED
                                                                    : TIRED_RUNTIME_FAILED;
    return tired_error_set(error, status, code, message, number);
}
static bool directory_check(TiredPublication *publication, TiredError *error)
{
    if (!tired_directory_check(publication->directory, error))
        return false;
    struct stat status;
    if (fstat(tired_directory_fd(publication->directory), &status) != 0)
        return io_error(error, "publication-directory", "Cannot inspect publication directory.");
    if (status.st_uid != geteuid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "publication-owner",
                               "Publication must run as the destination directory owner.", 0);
    if (publication->mode == 0600 && (status.st_mode & 07777) != 0700)
        return tired_error_set(error, TIRED_CONFLICT, "publication-private-directory",
                               "Private data publication requires a 0700 directory.", 0);
    return true;
}
static bool binding(TiredPublication *publication, bool unchanged, TiredError *error)
{
    if (!directory_check(publication, error))
        return false;
    struct stat file, entry;
    const char *name = publication->published ? publication->name : publication->temporary;
    if (fstat(publication->fd, &file) != 0 ||
        fstatat(tired_directory_fd(publication->directory), name, &entry, AT_SYMLINK_NOFOLLOW) != 0)
        return io_error(error, "publication-binding", "Cannot inspect publication file binding.");
    if (!S_ISREG(file.st_mode) || !S_ISREG(entry.st_mode) || file.st_uid != geteuid() ||
        file.st_nlink != 1 || file.st_dev != entry.st_dev || file.st_ino != entry.st_ino ||
        (publication->ready && (file.st_mode & 07777) != publication->mode))
        return tired_error_set(error, TIRED_CONFLICT, "publication-changed",
                               "Publication file was replaced or has unsafe metadata.", 0);
    const struct stat *before = &publication->prepared_identity;
    if (unchanged &&
        (file.st_size != before->st_size || file.st_mtim.tv_sec != before->st_mtim.tv_sec ||
         file.st_mtim.tv_nsec != before->st_mtim.tv_nsec ||
         (!publication->published && (file.st_ctim.tv_sec != before->st_ctim.tv_sec ||
                                      file.st_ctim.tv_nsec != before->st_ctim.tv_nsec))))
        return tired_error_set(error, TIRED_CONFLICT, "publication-modified",
                               "Staged file changed after preparation.", 0);
    return true;
}
bool tired_publication_prepare(TiredDirectory *directory, const char *name, const char *data,
                               size_t length, unsigned mode, TiredPublication **output,
                               TiredError *error)
{
    assert(directory != NULL && name != NULL && output != NULL && *output == NULL);
    assert(data != NULL || length == 0);
    size_t name_length = strnlen(name, 256);
    if (name_length == 0 || name_length > 255 || strchr(name, '/') != NULL ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        !tired_validate_text(name, name_length, true, error) || length > TIRED_PRIVATE_FILE_LIMIT ||
        (mode != 0600 && mode != 0644))
        return tired_error_set(error, TIRED_INVALID, "publication-input",
                               "Invalid publication input.", 0);
    TiredPublication *publication = calloc(1, sizeof(*publication));
    if (publication == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate publication.",
                               errno);
    publication->directory = directory;
    publication->fd = -1;
    publication->mode = mode;
    memcpy(publication->name, name, name_length + 1);
    char uuid[37];
    if (!directory_check(publication, error) || !tired_uuid_create(uuid, error))
        goto failed;
    (void)snprintf(publication->temporary, sizeof(publication->temporary), ".tired-%s.tmp", uuid);
    publication->fd = openat(tired_directory_fd(directory), publication->temporary,
                             O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (publication->fd < 0)
    {
        io_error(error, "publication-stage", "Cannot create publication staging file.");
        goto failed;
    }
    publication->staged = true;
    *output = publication;
    if (!binding(publication, false, error))
        return false;
    size_t written = 0;
    while (written < length)
    {
        size_t count = length - written;
        if (count > 65536)
            count = 65536;
        ssize_t bytes = write(publication->fd, data + written, count);
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0)
        {
            if (bytes == 0)
                errno = EIO;
            return io_error(error, "publication-write", "Cannot finish staged publication bytes.");
        }
        written += (size_t)bytes;
    }
    if (fchmod(publication->fd, mode) != 0 || fsync(publication->fd) != 0 ||
        fstat(publication->fd, &publication->prepared_identity) != 0)
        return io_error(error, "publication-prepare", "Cannot prepare durable publication bytes.");
    /* Check writer close before publication while retaining an inode reference. */
    int retained = fcntl(publication->fd, F_DUPFD_CLOEXEC, 3);
    if (retained < 0)
        return io_error(error, "publication-retain", "Cannot retain publication identity.");
    int writer = publication->fd;
    publication->fd = retained;
    if (close(writer) != 0)
        return io_error(error, "publication-close", "Cannot close publication writer.");
    if (fsync(tired_directory_fd(directory)) != 0)
        return io_error(error, "publication-stage-sync",
                        "Cannot make staging directory entry durable.");
    publication->ready = true;
    if (!binding(publication, true, error))
        return false;
    tired_error_clear(error);
    return true;
failed:
    tired_publication_destroy(publication);
    return false;
}
bool tired_publication_commit(TiredPublication *publication, const TiredOperationLock *lock,
                              TiredError *error)
{
    assert(publication != NULL && lock != NULL);
    if (publication->exchanged)
        return tired_publication_replace(publication, lock, &publication->previous, error);
    if (!publication->ready || (!publication->staged && !publication->published))
        return tired_error_set(error, TIRED_INVALID, "publication-not-ready",
                               "Publication is not prepared or was discarded.", 0);
    if (!tired_operation_lock_check(lock, error) || !binding(publication, true, error))
        return false;
    int directory = tired_directory_fd(publication->directory);
    if (!publication->published)
    {
        if (renameat2(directory, publication->temporary, directory, publication->name,
                      RENAME_NOREPLACE) != 0)
            return io_error(error, "publication-rename",
                            "Cannot publish without replacing an existing entry.");
        publication->published = true;
        publication->staged = false;
    }
    if (fsync(directory) != 0)
        return io_error(error, "publication-sync",
                        "File is published but directory durability is unknown.");
    publication->durable = true;
    tired_error_clear(error);
    return true;
}
bool tired_publication_replace(TiredPublication *publication, const TiredOperationLock *lock,
                               const TiredFileFingerprint *expected, TiredError *error)
{
    assert(publication != NULL && lock != NULL && expected != NULL);
    if (!expected->exists || !publication->ready ||
        (!publication->staged && !publication->published) ||
        (publication->published && !publication->exchanged))
        return tired_error_set(
            error, TIRED_INVALID, "replacement-input",
            "Replacement requires prepared staging and an existing before-state.", 0);
    if (publication->exchanged && !tired_file_fingerprint_equal(expected, &publication->previous))
        return tired_error_set(error, TIRED_CONFLICT, "replacement-expected",
                               "Cannot change replacement expectations after publication.", 0);
    TiredText encoded = {0};
    bool valid = tired_file_fingerprint_encode(expected, &encoded, error);
    tired_text_destroy(&encoded);
    if (!valid || !tired_operation_lock_check(lock, error) || !binding(publication, true, error))
        return false;
    TiredFileFingerprint actual = {0};
    int directory = tired_directory_fd(publication->directory);
    if (!publication->exchanged)
    {
        if (!tired_file_fingerprint(publication->directory, publication->name,
                                    (size_t)expected->size, &actual, error))
            return false;
        if (!tired_file_fingerprint_equal(expected, &actual))
            return tired_error_set(error, TIRED_CONFLICT, "replacement-changed",
                                   "Replacement destination differs from its before-state.", 0);
        if (renameat2(directory, publication->temporary, directory, publication->name,
                      RENAME_EXCHANGE) != 0)
            return io_error(error, "replacement-exchange",
                            "Cannot atomically exchange managed files.");
        publication->previous = *expected;
        publication->exchanged = publication->published = true;
        publication->staged = false;
    }
    if (fsync(directory) != 0)
        return io_error(error, "replacement-sync",
                        "Files were exchanged but durability is unknown.");
    if (!binding(publication, true, error) ||
        !tired_file_fingerprint(publication->directory, publication->temporary,
                                (size_t)expected->size, &actual, error))
        return false;
    if (!tired_file_fingerprint_equal(expected, &actual))
        return tired_error_set(
            error, TIRED_CONFLICT, "replacement-displaced",
            "Displaced file differs from its before-state; recovery inspection is required.", 0);
    publication->durable = true;
    tired_error_clear(error);
    return true;
}
bool tired_publication_published(const TiredPublication *publication)
{
    assert(publication != NULL);
    return publication->published;
}
bool tired_publication_durable(const TiredPublication *publication)
{
    assert(publication != NULL);
    return publication->durable;
}
const char *tired_publication_temporary_name(const TiredPublication *publication)
{
    assert(publication != NULL);
    return publication->temporary;
}
bool tired_publication_discard(TiredPublication *publication, TiredError *error)
{
    assert(publication != NULL);
    if (publication->published)
    {
        tired_error_clear(error);
        return true;
    }
    if (publication->staged)
    {
        if (!binding(publication, false, error))
            return false;
        if (unlinkat(tired_directory_fd(publication->directory), publication->temporary, 0) != 0)
            return io_error(error, "publication-discard",
                            "Cannot remove unpublished staging file.");
        publication->staged = false;
    }
    if (fsync(tired_directory_fd(publication->directory)) != 0)
        return io_error(error, "publication-discard-sync", "Cannot sync staging removal.");
    tired_error_clear(error);
    return true;
}
void tired_publication_destroy(TiredPublication *publication)
{
    if (publication == NULL)
        return;
    if (publication->fd >= 0)
        (void)close(publication->fd);
    free(publication);
}
