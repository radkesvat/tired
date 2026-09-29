#include "tired/verify.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct TiredVerification
{
    TiredStage *stage;
    TiredProcess *process;
    TiredVerifyState state;
    bool done, cleanup_complete;
    TiredError cleanup_error;
};
static bool verifier_trusted(TiredError *error)
{
    static const char *parts[] = {"usr", "bin", "systemd-analyze"};
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (unsigned i = 0; i < 4; ++i)
    {
        struct stat status;
        if (fd < 0 || fstat(fd, &status) != 0)
        {
            int saved = errno;
            if (fd >= 0)
                (void)close(fd);
            return tired_error_set(error, TIRED_UNSUPPORTED, "verifier-open",
                                   "Cannot inspect /usr/bin/systemd-analyze or its ancestors.",
                                   saved);
        }
        bool directory = i < 3;
        if (status.st_uid != 0 || (status.st_mode & 0022) != 0 ||
            (directory ? !S_ISDIR(status.st_mode) : !S_ISREG(status.st_mode)) ||
            (!directory && ((status.st_mode & 0111) == 0 || (status.st_mode & 06000) != 0)))
        {
            (void)close(fd);
            return tired_error_set(
                error, TIRED_CONFLICT, "verifier-trust",
                "Verifier path must be root-owned, non-writable by other users and non-setid.", 0);
        }
        if (i == 3)
            break;
        int next =
            openat(fd, parts[i],
                   O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (i < 2 ? O_DIRECTORY : 0));
        int saved = errno;
        (void)close(fd);
        fd = next;
        errno = saved;
    }
    (void)close(fd);
    return true;
}
static bool environment_path(TiredText *output, const char *key, const char *path,
                             TiredError *error)
{
    if (path == NULL || path[0] != '/')
        return tired_error_set(error, TIRED_INVALID, "verify-user-path",
                               "User verification requires explicit absolute user locations.", 0);
    size_t length = strnlen(path, TIRED_INPUT_LIMIT + 1);
    if (length > TIRED_INPUT_LIMIT || !tired_validate_text(path, length, true, error))
        return tired_error_set(
            error, TIRED_INVALID, "verify-user-path",
            "User verification location exceeds bounds or contains invalid text.", 0);
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT);
    bool ok = tired_buffer_append(&buffer, key, strlen(key), error) &&
              tired_buffer_append(&buffer, path, length, error) &&
              tired_buffer_take(&buffer, output, error);
    tired_buffer_destroy(&buffer);
    return ok;
}
bool tired_verify_cleanup(TiredVerification *verification, TiredError *error)
{
    assert(verification != NULL && verification->done);
    verification->cleanup_complete =
        verification->stage == NULL ||
        tired_stage_remove(verification->stage, &verification->cleanup_error);
    if (verification->cleanup_complete)
        tired_error_clear(&verification->cleanup_error);
    if (error != NULL)
        *error = verification->cleanup_error;
    return verification->cleanup_complete;
}
bool tired_verify_start(const TiredText *base, const char *unit, size_t length,
                        const TiredVerifyUserPaths *user, unsigned timeout_ms,
                        TiredVerification **output, TiredError *error)
{
    assert(output != NULL && *output == NULL);
    if (getuid() != geteuid() || getgid() != getegid())
        return tired_error_set(error, TIRED_AUTHORIZATION, "verify-identity",
                               "Verification requires matching real and effective identities.", 0);
    if (!verifier_trusted(error))
        return false;
    TiredText paths[4] = {0};
    bool ok = false;
    TiredVerification *verification = calloc(1, sizeof(*verification));
    if (verification == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate verification.",
                               errno);
    char *environment[] = {"LC_ALL=C",
                           "PATH=/usr/bin:/bin",
                           "SYSTEMD_COLORS=0",
                           "SYSTEMD_LOG_TARGET=console",
                           NULL,
                           NULL,
                           NULL,
                           NULL,
                           NULL};
    if (user != NULL)
    {
        if (!environment_path(&paths[0], "HOME=", user->home, error) ||
            !environment_path(&paths[1], "XDG_CONFIG_HOME=", user->config_home, error) ||
            !environment_path(&paths[2], "XDG_DATA_HOME=", user->data_home, error) ||
            !environment_path(&paths[3], "XDG_RUNTIME_DIR=", user->runtime_directory, error))
            goto done;
        for (unsigned i = 0; i < 4; ++i)
            environment[i + 4] = paths[i].data;
    }
    if (!tired_stage_create(base, unit, length, &verification->stage, error))
        goto done;
    char *arguments[] = {"/usr/bin/systemd-analyze",
                         user == NULL ? "--system" : "--user",
                         "--no-pager",
                         "--man=no",
                         "--generators=no",
                         "verify",
                         (char *)tired_stage_unit_path(verification->stage),
                         NULL};
    ok = tired_process_start(arguments[0], arguments, environment, timeout_ms, TIRED_INPUT_LIMIT,
                             &verification->process, error);
done:
    for (unsigned i = 0; i < 4; ++i)
        tired_text_destroy(&paths[i]);
    *output = verification;
    if (!ok)
    {
        verification->done = true;
        verification->state = TIRED_VERIFY_INCOMPLETE;
        (void)tired_verify_cleanup(verification, NULL);
    }
    return ok;
}
bool tired_verify_step(TiredVerification *verification)
{
    assert(verification != NULL);
    if (verification->done)
        return true;
    if (!tired_process_step(verification->process))
        return false;
    TiredProcessResult result = tired_process_result(verification->process);
    verification->state = result.outcome != TIRED_PROCESS_EXITED ? TIRED_VERIFY_INCOMPLETE
                          : result.exit_code != 0                ? TIRED_VERIFY_NONZERO
                          : result.output_length != 0 || result.error_length != 0
                              ? TIRED_VERIFY_DIAGNOSTICS
                              : TIRED_VERIFY_CLEAN;
    verification->done = true;
    (void)tired_verify_cleanup(verification, NULL);
    return true;
}
void tired_verify_cancel(TiredVerification *verification)
{
    assert(verification != NULL);
    if (!verification->done)
        tired_process_cancel(verification->process);
}
TiredVerifyResult tired_verify_result(const TiredVerification *verification)
{
    assert(verification != NULL);
    return (TiredVerifyResult){
        .state = verification->state,
        .process = verification->process == NULL
                       ? (TiredProcessResult){.outcome = TIRED_PROCESS_IO_ERROR, .exit_code = -1}
                       : tired_process_result(verification->process),
        .cleanup_complete = verification->cleanup_complete,
        .cleanup_error = verification->cleanup_error,
        .retained_directory = !verification->cleanup_complete && verification->stage != NULL
                                  ? tired_stage_directory(verification->stage)
                                  : NULL};
}
void tired_verify_destroy(TiredVerification *verification)
{
    if (verification == NULL)
        return;
    assert(verification->done);
    tired_process_destroy(verification->process);
    tired_stage_destroy(verification->stage);
    free(verification);
}
