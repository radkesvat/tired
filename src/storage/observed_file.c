#include "tired/observed_file.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>
#include <sys/vfs.h>
#include <unistd.h>

void tired_observed_file_destroy(TiredObservedFile *file)
{
    if (file == NULL)
        return;
    if (file->bytes.data != NULL)
        OPENSSL_cleanse(file->bytes.data, file->bytes.length);
    tired_text_destroy(&file->bytes);
    tired_text_destroy(&file->resolved_path);
    *file = (TiredObservedFile){0};
}
static bool io_error(TiredError *error)
{
    int number = errno;
    return tired_error_set(error,
                           number == ENOENT                      ? TIRED_NOT_FOUND
                           : number == EACCES || number == EPERM ? TIRED_AUTHORIZATION
                                                                 : TIRED_RUNTIME_FAILED,
                           "observed-path-io", "Cannot inspect reported configuration path.",
                           number);
}
static bool conflict(TiredError *error)
{
    return tired_error_set(error, TIRED_CONFLICT, "observed-path-trust",
                           "Reported configuration path has unsafe or changing directory aliases.",
                           0);
}
static bool invalid(TiredError *error)
{
    return tired_error_set(
        error, TIRED_INVALID, "observed-path",
        "Reported configuration path exceeds supported syntax or resolution bounds.", 0);
}
static void parent(char path[4096])
{
    char *slash = strrchr(path, '/');
    assert(slash != NULL);
    if (slash == path)
        path[1] = '\0';
    else
        *slash = '\0';
}
static bool same_link(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_uid == b->st_uid &&
           a->st_gid == b->st_gid && a->st_mode == b->st_mode && a->st_size == b->st_size &&
           a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
           a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
static bool resolve(char pending[4096], uid_t owner, char resolved[4096], TiredDirectory **output,
                    TiredError *error)
{
    TiredDirectory *directory = NULL;
    memcpy(resolved, "/", 2);
    if (!tired_directory_open(resolved, owner, false, &directory, error))
        return false;
    size_t offset = 0;
    unsigned links = 0;
    bool ok = false;
    while (pending[offset] != '\0')
    {
        if (pending[offset] == '/')
        {
            ++offset;
            continue;
        }
        size_t start = offset;
        while (pending[offset] != '\0' && pending[offset] != '/')
            ++offset;
        size_t length = offset - start;
        if (length > 255)
        {
            invalid(error);
            goto done;
        }
        char component[256];
        memcpy(component, pending + start, length);
        component[length] = '\0';
        if (strcmp(component, ".") == 0)
            continue;
        if (strcmp(component, "..") == 0)
        {
            parent(resolved);
            tired_directory_destroy(directory);
            directory = NULL;
            if (!tired_directory_open(resolved, owner, false, &directory, error))
                goto done;
            continue;
        }
        if (!tired_directory_check(directory, error))
            goto done;
        struct stat before, after;
        int fd = tired_directory_fd(directory);
        if (fstatat(fd, component, &before, AT_SYMLINK_NOFOLLOW) != 0)
        {
            io_error(error);
            goto done;
        }
        if (S_ISLNK(before.st_mode))
        {
            struct statfs filesystem;
            if (fstatfs(fd, &filesystem) != 0)
            {
                io_error(error);
                goto done;
            }
            /* procfs magic links can refer into another namespace; their text
             * is not an ordinary pathname substitution. */
            if (filesystem.f_type == PROC_SUPER_MAGIC)
            {
                conflict(error);
                goto done;
            }
            if (++links > 32 || (before.st_uid != 0 && before.st_uid != owner))
            {
                conflict(error);
                goto done;
            }
            char target[4096], next[4096];
            ssize_t count = readlinkat(fd, component, target, sizeof(target));
            if (count < 0 || fstatat(fd, component, &after, AT_SYMLINK_NOFOLLOW) != 0)
            {
                io_error(error);
                goto done;
            }
            if (!same_link(&before, &after) || !tired_directory_check(directory, error))
            {
                conflict(error);
                goto done;
            }
            if (count == 0 || (size_t)count >= sizeof(target) ||
                !tired_validate_text(target, (size_t)count, true, error))
            {
                invalid(error);
                goto done;
            }
            target[count] = '\0';
            size_t rest = strlen(pending + offset);
            if ((size_t)count + rest >= sizeof(next))
            {
                invalid(error);
                goto done;
            }
            memcpy(next, target, (size_t)count);
            memcpy(next + count, pending + offset, rest + 1);
            memcpy(pending, next, (size_t)count + rest + 1);
            offset = 0;
            if (target[0] == '/')
            {
                memcpy(resolved, "/", 2);
                tired_directory_destroy(directory);
                directory = NULL;
                if (!tired_directory_open(resolved, owner, false, &directory, error))
                    goto done;
            }
            continue;
        }
        if (!S_ISDIR(before.st_mode))
        {
            conflict(error);
            goto done;
        }
        size_t used = strlen(resolved), separator = used == 1 ? 0 : 1;
        if (used + separator + length >= 4096)
        {
            invalid(error);
            goto done;
        }
        TiredDirectory *child = NULL;
        if (!tired_directory_child(directory, component, false, false, &child, error))
            goto done;
        tired_directory_destroy(directory);
        directory = child;
        if (separator != 0)
            resolved[used++] = '/';
        memcpy(resolved + used, component, length + 1);
    }
    *output = directory;
    directory = NULL;
    ok = true;
done:
    tired_directory_destroy(directory);
    return ok;
}
bool tired_observed_file_read(const char *path, bool user_scope, size_t *remaining,
                              TiredObservedFile *output, TiredError *error)
{
    assert(path != NULL && remaining != NULL && output != NULL);
    size_t length = strnlen(path, 4096);
    if (length < 2 || length >= 4096 || path[0] != '/' ||
        !tired_validate_text(path, length, true, error))
        return invalid(error);
    const char *name = strrchr(path, '/') + 1;
    size_t name_length = strlen(name);
    if (name_length == 0 || name_length > 255 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return invalid(error);
    char pending[4096], resolved[4096], full[4096];
    size_t parent_length = (size_t)(name - path);
    memcpy(pending, path, parent_length);
    pending[parent_length] = '\0';
    TiredDirectory *directory = NULL;
    TiredObservedFile file = {0};
    bool ok = false;
    if (!resolve(pending, user_scope ? geteuid() : 0, resolved, &directory, error))
        goto done;
    int count = snprintf(full, sizeof(full), "%s%s%s", resolved,
                         strcmp(resolved, "/") == 0 ? "" : "/", name);
    if (count < 0 || (size_t)count >= sizeof(full))
    {
        invalid(error);
        goto done;
    }
    if (!tired_text_set(&file.resolved_path, full, (size_t)count, 4095, error))
        goto done;
    if (*remaining == 0)
    {
        tired_error_set(error, TIRED_RECOVERY_REQUIRED, "observed-file-budget",
                        "Reported configuration file read budget exhausted.", 0);
        goto done;
    }
    size_t limit = *remaining < TIRED_UNIT_LIMIT ? *remaining : TIRED_UNIT_LIMIT;
    bool known = tired_file_snapshot(directory, name, limit, &file.fingerprint, &file.bytes, error);
    *remaining -= known ? (size_t)file.fingerprint.size : limit;
    if (!known)
        goto done;
    tired_observed_file_destroy(output);
    *output = file;
    file = (TiredObservedFile){0};
    tired_error_clear(error);
    ok = true;
done:
    tired_observed_file_destroy(&file);
    tired_directory_destroy(directory);
    return ok;
}
