#include "tired/capture.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool tired_validate_text(const char *data, size_t length, bool path, TiredError *error)
{
    assert(data != NULL || length == 0);
    for (size_t i = 0; i < length;)
    {
        unsigned char c = (unsigned char)data[i++];
        if (c == 0 || (path && (c < 32 || c == 127)))
            return tired_error_set(error, TIRED_INVALID, "text-control",
                                   "Text contains a control byte unsupported in this field.", 0);
        if (c < 128)
            continue;
        size_t continuation;
        uint32_t scalar;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf)
        {
            continuation = 1;
            scalar = c & 0x1fU;
            minimum = 0x80;
        }
        else if (c >= 0xe0 && c <= 0xef)
        {
            continuation = 2;
            scalar = c & 0x0fU;
            minimum = 0x800;
        }
        else if (c >= 0xf0 && c <= 0xf4)
        {
            continuation = 3;
            scalar = c & 0x07U;
            minimum = 0x10000;
        }
        else
            goto invalid;
        if (continuation > length - i)
            goto invalid;
        for (size_t j = 0; j < continuation; ++j)
        {
            unsigned char next = (unsigned char)data[i++];
            if ((next & 0xc0U) != 0x80U)
                goto invalid;
            scalar = (scalar << 6) | (next & 0x3fU);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            goto invalid;
    }
    tired_error_clear(error);
    return true;
invalid:
    return tired_error_set(error, TIRED_INVALID, "invalid-utf8", "Text is not valid UTF-8.", 0);
}

void tired_invocation_destroy(TiredInvocation *invocation)
{
    if (invocation == NULL)
        return;
    tired_text_list_destroy(&invocation->argv);
    tired_text_destroy(&invocation->directory);
    tired_text_destroy(&invocation->executable);
    tired_text_destroy(&invocation->resolved_target);
    *invocation = (TiredInvocation){0};
}

static bool capture_directory(TiredText *directory, TiredError *error)
{
    size_t capacity = 256;
    while (capacity <= TIRED_INPUT_LIMIT)
    {
        char *buffer = malloc(capacity);
        if (buffer == NULL)
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot allocate directory path.", errno);
        if (getcwd(buffer, capacity) != NULL)
        {
            size_t length = strlen(buffer);
            if (!tired_validate_text(buffer, length, true, error))
            {
                free(buffer);
                return false;
            }
            *directory = (TiredText){buffer, length};
            return true;
        }
        int saved_errno = errno;
        free(buffer);
        if (saved_errno != ERANGE)
            return tired_error_set(error, TIRED_INVALID, "working-directory",
                                   "Cannot capture the current directory.", saved_errno);
        capacity *= 2;
    }
    return tired_error_set(error, TIRED_INVALID, "input-limit",
                           "Working directory exceeds the path limit.", 0);
}

static bool join_path(const TiredText *directory, const char *tail, size_t tail_length,
                      TiredText *result, TiredError *error)
{
    if (tail_length != 0 && tail[0] == '/')
        return tired_text_set(result, tail, tail_length, TIRED_INPUT_LIMIT, error);
    if (directory->length >= TIRED_INPUT_LIMIT ||
        tail_length >= TIRED_INPUT_LIMIT - directory->length)
        return tired_error_set(error, TIRED_INVALID, "input-limit",
                               "Executable path exceeds the path limit.", 0);
    size_t length = directory->length + 1 + tail_length;
    char *buffer = malloc(length + 1);
    if (buffer == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot allocate executable path.", errno);
    memcpy(buffer, directory->data, directory->length);
    buffer[directory->length] = '/';
    memcpy(buffer + directory->length + 1, tail, tail_length);
    buffer[length] = '\0';
    tired_text_destroy(result);
    *result = (TiredText){buffer, length};
    return true;
}

static bool inspect_executable(TiredInvocation *invocation, TiredError *error)
{
    struct stat status;
    if (!tired_validate_text(invocation->executable.data, invocation->executable.length, true,
                             error))
        return false;
    if (stat(invocation->executable.data, &status) != 0)
        return tired_error_set(error, TIRED_INVALID, "executable-unavailable",
                               "Cannot inspect the executable.", errno);
    if (!S_ISREG(status.st_mode))
        return tired_error_set(error, TIRED_INVALID, "executable-type",
                               "Executable must be a regular file.", 0);
    if (access(invocation->executable.data, X_OK) != 0)
        return tired_error_set(error, TIRED_INVALID, "executable-permission",
                               "Executable is not executable by the invoking identity.", errno);
    char *resolved = realpath(invocation->executable.data, NULL);
    if (resolved == NULL)
        return tired_error_set(error, errno == ENOMEM ? TIRED_INTERNAL : TIRED_INVALID,
                               "executable-target", "Cannot resolve the executable target.", errno);
    struct stat target_status;
    if (stat(resolved, &target_status) != 0 || status.st_dev != target_status.st_dev ||
        status.st_ino != target_status.st_ino)
    {
        free(resolved);
        return tired_error_set(error, TIRED_CONFLICT, "executable-changed",
                               "Executable changed during capture; capture it again.", 0);
    }
    size_t length = strlen(resolved);
    bool ok =
        tired_validate_text(resolved, length, true, error) &&
        tired_text_set(&invocation->resolved_target, resolved, length, TIRED_INPUT_LIMIT, error);
    free(resolved);
    if (!ok)
        return false;
    invocation->device = status.st_dev;
    invocation->inode = status.st_ino;
    return true;
}

bool tired_invocation_capture(const TiredTextList *arguments, const char *path,
                              TiredInvocation *result, TiredError *error)
{
    assert(arguments != NULL && result != NULL);
    TiredError local_error = {0};
    if (error == NULL)
        error = &local_error;
    if (arguments->count == 0 || arguments->count > TIRED_ARGUMENT_LIMIT ||
        arguments->items[0].length == 0)
        return tired_error_set(error, TIRED_INVALID, "command-count",
                               "Expected a nonempty command with at most 4096 arguments.", 0);
    TiredInvocation captured = {0};
    captured.uid = getuid();
    captured.gid = getgid();
    if (!capture_directory(&captured.directory, error))
        goto fail;
    for (size_t i = 0; i < arguments->count; ++i)
    {
        const TiredText *arg = &arguments->items[i];
        if (arg->length > TIRED_INPUT_LIMIT ||
            !tired_validate_text(arg->data, arg->length, i == 0, error) ||
            !tired_text_list_append(&captured.argv, arg->data, arg->length, TIRED_ARGUMENT_LIMIT,
                                    TIRED_INPUT_LIMIT, error))
        {
            if (arg->length > TIRED_INPUT_LIMIT)
                tired_error_set(error, TIRED_INVALID, "input-limit",
                                "Argument exceeds the command limit.", 0);
            goto fail;
        }
    }
    const TiredText *command = &captured.argv.items[0];
    if (memchr(command->data, '/', command->length) != NULL)
    {
        if (!join_path(&captured.directory, command->data, command->length, &captured.executable,
                       error) ||
            !inspect_executable(&captured, error))
            goto fail;
    }
    else
    {
        if (path == NULL)
        {
            tired_error_set(error, TIRED_INVALID, "path-unset",
                            "PATH is absent; use an explicit executable path.", 0);
            goto fail;
        }
        size_t path_length = strnlen(path, TIRED_INPUT_LIMIT + 1);
        if (path_length > TIRED_INPUT_LIMIT)
        {
            tired_error_set(error, TIRED_INVALID, "input-limit", "PATH exceeds the input limit.",
                            0);
            goto fail;
        }
        size_t offset = 0;
        bool found = false;
        do
        {
            size_t end = offset;
            while (end < path_length && path[end] != ':')
                ++end;
            TiredText search_directory = {0};
            if (!join_path(&captured.directory, path + offset, end - offset, &search_directory,
                           error))
                goto fail;
            bool joined = join_path(&search_directory, command->data, command->length,
                                    &captured.executable, error);
            tired_text_destroy(&search_directory);
            if (!joined)
                goto fail;
            if (inspect_executable(&captured, error))
            {
                found = true;
                break;
            }
            if (error->status == TIRED_INTERNAL || error->status == TIRED_CONFLICT)
                goto fail;
            offset = end + 1;
        } while (offset <= path_length);
        if (!found)
        {
            tired_error_set(error, TIRED_INVALID, "command-not-found",
                            "No regular executable was found in the invoking PATH.", 0);
            goto fail;
        }
    }
    tired_invocation_destroy(result);
    *result = captured;
    tired_error_clear(error);
    return true;
fail:
    tired_invocation_destroy(&captured);
    return false;
}
