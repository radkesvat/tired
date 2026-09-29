#define _GNU_SOURCE
#include "tired/process.h"
#include "tired/encode.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct TiredProcess
{
    pid_t pid;
    int pipes[2];
    TiredBuffer buffers[2];
    uint64_t deadline;
    TiredProcessOutcome reason;
    int status, system_errno;
    bool done, reaped;
};
static bool now_ms(uint64_t *value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return false;
    *value = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return true;
}
static void close_pipes(TiredProcess *process)
{
    for (unsigned i = 0; i < 2; ++i)
        if (process->pipes[i] >= 0)
        {
            (void)close(process->pipes[i]);
            process->pipes[i] = -1;
        }
}
static void stop(TiredProcess *process, TiredProcessOutcome reason, int system_errno)
{
    if (process->done || process->reason != TIRED_PROCESS_RUNNING)
        return;
    process->reason = reason;
    process->system_errno = system_errno;
    /* The child remains unreaped until we finish the group signal, preventing
     * reuse of its PID as an unrelated process-group ID. */
    (void)kill(-process->pid, SIGKILL);
    close_pipes(process);
}
static bool vector_bytes(char *const vector[], size_t *total)
{
    if (vector == NULL)
        return false;
    for (size_t i = 0; i <= TIRED_ARGUMENT_LIMIT; ++i)
    {
        if (vector[i] == NULL)
            return true;
        if (i == TIRED_ARGUMENT_LIMIT)
            return false;
        size_t size = strnlen(vector[i], TIRED_INPUT_LIMIT);
        if (size >= TIRED_INPUT_LIMIT - *total)
            return false;
        *total += size + 1;
    }
    return false;
}
static int move_fd(int fd)
{
    if (fd < 0 || fd >= 3)
        return fd;
    int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    int saved = errno;
    (void)close(fd);
    errno = saved;
    return moved;
}
bool tired_process_start(const char *executable, char *const argv[], char *const environment[],
                         unsigned timeout_ms, size_t output_limit, TiredProcess **output,
                         TiredError *error)
{
    assert(executable != NULL && output != NULL && *output == NULL);
    size_t bytes = strnlen(executable, TIRED_INPUT_LIMIT);
    if (bytes == 0 || bytes >= TIRED_INPUT_LIMIT || executable[0] != '/' ||
        !vector_bytes(argv, &bytes) || argv[0] == NULL || !vector_bytes(environment, &bytes) ||
        timeout_ms == 0 || timeout_ms > 300000 || output_limit > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "process-input",
                               "Invalid subprocess path, vectors, deadline, or output bound.", 0);
    struct sigaction child_action;
    if (sigaction(SIGCHLD, NULL, &child_action) != 0 || child_action.sa_handler == SIG_IGN ||
        (child_action.sa_flags & SA_NOCLDWAIT) != 0)
        return tired_error_set(error, TIRED_INVALID, "process-reaping",
                               "Subprocess capture requires waitable children.", 0);
    TiredProcess *process = calloc(1, sizeof(*process));
    if (process == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate subprocess.",
                               errno);
    process->pipes[0] = process->pipes[1] = -1;
    int writers[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    bool actions_ready = false, attributes_ready = false;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc != 0)
        goto fail;
    actions_ready = true;
    rc = posix_spawnattr_init(&attributes);
    if (rc != 0)
        goto fail;
    attributes_ready = true;
    for (unsigned i = 0; i < 2; ++i)
    {
        tired_buffer_init(&process->buffers[i], output_limit);
        int pair[2];
        if (pipe2(pair, O_CLOEXEC) != 0)
        {
            rc = errno;
            goto fail;
        }
        process->pipes[i] = move_fd(pair[0]);
        writers[i] = move_fd(pair[1]);
        if (process->pipes[i] < 0 || writers[i] < 0 ||
            fcntl(process->pipes[i], F_SETFL, O_NONBLOCK) != 0)
        {
            rc = errno;
            goto fail;
        }
        rc = posix_spawn_file_actions_adddup2(&actions, writers[i], (int)i + 1);
        if (rc != 0)
            goto fail;
    }
    rc = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    if (rc == 0)
        rc = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    sigset_t empty, defaults;
    (void)sigemptyset(&empty);
    (void)sigfillset(&defaults);
    if (rc == 0)
        rc = posix_spawnattr_setsigmask(&attributes, &empty);
    if (rc == 0)
        rc = posix_spawnattr_setsigdefault(&attributes, &defaults);
    if (rc == 0)
        rc = posix_spawnattr_setpgroup(&attributes, 0);
    if (rc == 0)
        rc = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK |
                                                       POSIX_SPAWN_SETSIGDEF);
    if (rc != 0)
        goto fail;
    if (!now_ms(&process->deadline))
    {
        rc = errno;
        goto fail;
    }
    process->deadline += timeout_ms;
    rc = posix_spawn(&process->pid, executable, &actions, &attributes, argv, environment);
    if (rc != 0)
        goto fail;
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)posix_spawnattr_destroy(&attributes);
    (void)close(writers[0]);
    (void)close(writers[1]);
    *output = process;
    tired_error_clear(error);
    return true;
fail:
    if (actions_ready)
        (void)posix_spawn_file_actions_destroy(&actions);
    if (attributes_ready)
        (void)posix_spawnattr_destroy(&attributes);
    for (unsigned i = 0; i < 2; ++i)
    {
        if (writers[i] >= 0)
            (void)close(writers[i]);
        tired_buffer_destroy(&process->buffers[i]);
    }
    close_pipes(process);
    free(process);
    return tired_error_set(error, TIRED_INVALID, "process-start", "Cannot start subprocess.", rc);
}
void tired_process_cancel(TiredProcess *process)
{
    assert(process != NULL);
    stop(process, TIRED_PROCESS_CANCELLED, 0);
}
bool tired_process_step(TiredProcess *process)
{
    assert(process != NULL);
    if (process->done)
        return true;
    uint64_t now;
    if (!now_ms(&now))
        stop(process, TIRED_PROCESS_IO_ERROR, errno);
    else if (now >= process->deadline)
        stop(process, TIRED_PROCESS_TIMEOUT, 0);
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned reads = 0; process->pipes[i] >= 0 && reads < 16; ++reads)
        {
            char bytes[4096];
            ssize_t count = read(process->pipes[i], bytes, sizeof(bytes));
            if (count == 0)
            {
                (void)close(process->pipes[i]);
                process->pipes[i] = -1;
                break;
            }
            if (count < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == EINTR)
                    continue;
                stop(process, TIRED_PROCESS_IO_ERROR, errno);
                break;
            }
            TiredError error = {0};
            if (!tired_buffer_append(&process->buffers[i], bytes, (size_t)count, &error))
            {
                stop(process,
                     error.status == TIRED_INVALID ? TIRED_PROCESS_OUTPUT_LIMIT
                                                   : TIRED_PROCESS_IO_ERROR,
                     error.system_errno);
                break;
            }
        }
    /* Keep an exited leader unreaped while descendants still hold the pipes, so
     * deadline/cancellation can safely target the original process group. */
    if (process->pipes[0] < 0 && process->pipes[1] < 0)
    {
        pid_t waited = waitpid(process->pid, &process->status, WNOHANG);
        if (waited == process->pid)
        {
            process->done = true;
            process->reaped = true;
            if (process->reason == TIRED_PROCESS_RUNNING)
                process->reason =
                    WIFEXITED(process->status) ? TIRED_PROCESS_EXITED : TIRED_PROCESS_SIGNALED;
        }
        else if (waited < 0 && errno != EINTR)
        {
            process->system_errno = errno;
            process->reason = TIRED_PROCESS_IO_ERROR;
            process->done = true;
        }
    }
    return process->done;
}
TiredProcessResult tired_process_result(const TiredProcess *process)
{
    assert(process != NULL);
    return (TiredProcessResult){
        .outcome = process->done ? process->reason : TIRED_PROCESS_RUNNING,
        .exit_code =
            process->reaped && WIFEXITED(process->status) ? WEXITSTATUS(process->status) : -1,
        .signal_number =
            process->reaped && WIFSIGNALED(process->status) ? WTERMSIG(process->status) : 0,
        .system_errno = process->system_errno,
        .standard_output = process->buffers[0].data,
        .standard_error = process->buffers[1].data,
        .output_length = process->buffers[0].length,
        .error_length = process->buffers[1].length};
}
void tired_process_destroy(TiredProcess *process)
{
    if (process == NULL)
        return;
    assert(process->done);
    close_pipes(process);
    tired_buffer_destroy(&process->buffers[0]);
    tired_buffer_destroy(&process->buffers[1]);
    free(process);
}
