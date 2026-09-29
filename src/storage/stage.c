#include "tired/stage.h"
#include "tired/encode.h"
#include "tired/io.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct TiredStage
{
    int parent, directory, file;
    char component[64];
    TiredText filename, path, directory_path;
    struct stat directory_identity, file_identity;
    bool directory_created, directory_observed, file_created;
};
static bool failure(TiredError *error, const char *code, const char *message)
{
    return tired_error_set(error, TIRED_INVALID, code, message, errno);
}
void tired_stage_destroy(TiredStage *stage)
{
    if (stage == NULL)
        return;
    if (stage->file >= 0)
        (void)close(stage->file);
    if (stage->directory >= 0)
        (void)close(stage->directory);
    if (stage->parent >= 0)
        (void)close(stage->parent);
    tired_text_destroy(&stage->filename);
    tired_text_destroy(&stage->path);
    tired_text_destroy(&stage->directory_path);
    free(stage);
}
const char *tired_stage_unit_path(const TiredStage *stage)
{
    assert(stage != NULL);
    return stage->path.data;
}
const char *tired_stage_directory(const TiredStage *stage)
{
    assert(stage != NULL);
    return stage->directory_path.data;
}
static bool same(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}
bool tired_stage_remove(TiredStage *stage, TiredError *error)
{
    assert(stage != NULL);
    if (!stage->directory_created)
    {
        tired_error_clear(error);
        return true;
    }
    struct stat status;
    if (!stage->directory_observed ||
        fstatat(stage->parent, stage->component, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(status.st_mode) || !same(&status, &stage->directory_identity))
        return tired_error_set(error, TIRED_CONFLICT, "stage-directory-changed",
                               "Verification directory changed; automatic cleanup refused.", 0);
    if (stage->file_created)
    {
        if (fstatat(stage->directory, stage->filename.data, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISREG(status.st_mode) || !same(&status, &stage->file_identity))
            return tired_error_set(error, TIRED_CONFLICT, "stage-file-changed",
                                   "Staged unit changed; automatic cleanup refused.", 0);
        if (unlinkat(stage->directory, stage->filename.data, 0) != 0)
            return failure(error, "stage-unlink", "Cannot remove staged unit.");
        stage->file_created = false;
    }
    if (unlinkat(stage->parent, stage->component, AT_REMOVEDIR) != 0)
        return failure(error, "stage-rmdir", "Cannot remove verification directory.");
    stage->directory_created = false;
    tired_error_clear(error);
    return true;
}
bool tired_stage_create(const TiredText *base, const char *unit, size_t length, TiredStage **output,
                        TiredError *error)
{
    assert(base != NULL && output != NULL && *output == NULL && (unit != NULL || length == 0));
    if (!tired_name_validate_base(base->data, base->length, error))
        return false;
    if (length > TIRED_UNIT_LIMIT || (length != 0 && memchr(unit, '\0', length) != NULL))
        return tired_error_set(error, TIRED_INVALID, "stage-input",
                               "Invalid or oversized unit text.", 0);
    TiredStage *stage = calloc(1, sizeof(*stage));
    if (stage == NULL)
        return failure(error, "allocation", "Cannot allocate verification stage.");
    stage->parent = stage->directory = stage->file = -1;
    int file = -1;
    char uuid[37];
    TiredBuffer directory;
    tired_buffer_init(&directory, 128);
    if (!tired_uuid_create(uuid, error) || !tired_name_candidate(base, 1, &stage->filename, error))
        goto fail;
    memcpy(stage->component, "tired-verify-", 13);
    memcpy(stage->component + 13, uuid, sizeof(uuid));
    if (!tired_buffer_append(&directory, "/tmp/", 5, error) ||
        !tired_buffer_append(&directory, stage->component, strlen(stage->component), error) ||
        !tired_buffer_take(&directory, &stage->directory_path, error) ||
        !tired_path_absolute(&stage->directory_path, stage->filename.data, stage->filename.length,
                             &stage->path, error))
        goto fail;
    stage->parent = open("/tmp", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat status;
    if (stage->parent < 0 || fstat(stage->parent, &status) != 0)
    {
        failure(error, "stage-parent", "Cannot inspect temporary directory.");
        goto fail;
    }
    if (status.st_uid != 0 || ((status.st_mode & 0022) != 0 && (status.st_mode & S_ISVTX) == 0))
    {
        tired_error_set(
            error, TIRED_CONFLICT, "stage-parent-trust",
            "Temporary directory must be root-owned and protect entries from other users.", 0);
        goto fail;
    }
    if (mkdirat(stage->parent, stage->component, 0700) != 0)
    {
        failure(error, "stage-mkdir", "Cannot create private verification directory.");
        goto fail;
    }
    stage->directory_created = true;
    stage->directory =
        openat(stage->parent, stage->component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (stage->directory < 0 || fstat(stage->directory, &stage->directory_identity) != 0)
    {
        failure(error, "stage-open", "Cannot inspect private verification directory.");
        goto fail;
    }
    stage->directory_observed = true;
    if (fchmod(stage->directory, 0700) != 0)
    {
        failure(error, "stage-mode", "Cannot set private verification directory mode.");
        goto fail;
    }
    file = openat(stage->directory, stage->filename.data,
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0)
    {
        failure(error, "stage-create", "Cannot create staged unit.");
        goto fail;
    }
    stage->file = file;
    if (fstat(file, &stage->file_identity) != 0)
    {
        failure(error, "stage-stat", "Cannot inspect staged unit.");
        goto fail;
    }
    stage->file_created = true;
    if (fchmod(file, 0600) != 0)
    {
        failure(error, "stage-mode", "Cannot set private staged unit mode.");
        goto fail;
    }
    for (size_t written = 0; written < length;)
    {
        ssize_t count = write(file, unit + written, length - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            failure(error, "stage-write", "Cannot write staged unit.");
            goto fail;
        }
        written += (size_t)count;
    }
    if (fsync(file) != 0)
    {
        failure(error, "stage-sync", "Cannot synchronize staged unit.");
        goto fail;
    }
    if (fsync(stage->directory) != 0)
    {
        failure(error, "stage-sync", "Cannot synchronize verification directory.");
        goto fail;
    }
    tired_buffer_destroy(&directory);
    *output = stage;
    tired_error_clear(error);
    return true;
fail:
    tired_buffer_destroy(&directory);
    if (stage->directory_created)
        *output = stage;
    else
        tired_stage_destroy(stage);
    return false;
}
