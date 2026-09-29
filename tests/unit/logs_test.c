#include "tired/json.h"
#include "tired/logs_frontend.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static volatile sig_atomic_t stopped;
static void stop(int sig) { stopped = sig; }
static bool session(TiredRequest *request, FILE *output, TiredStatus *status, TiredError *error)
{
    const char *roots[] = {"runtime", "persistent"};
    sd_journal *journal = NULL;
    int rc = sd_journal_open_directory(&journal, "runtime", 0);
    if (rc < 0)
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "fixture", "Cannot open fixture.", -rc);
    bool ok = tired_logs_session(request, journal, roots, "0123456789abcdef0123456789abcdef", 200,
                                 output, &stopped, status, error);
    sd_journal_close(journal);
    return ok;
}
int main(void)
{
    int result = 1, saved = -1, fd = -1, pipes[2] = {-1, -1};
    pid_t child = -1;
    char fixture[] = "logs-test-XXXXXX";
    char *directory = NULL;
    bool entered = false;
    FILE *output = NULL;
    TiredRequest request = {0};
    TiredError error = {0};
    TiredStatus status = TIRED_INTERNAL;
    struct json_object *object = NULL, *value = NULL;
    char line[4096];
    const char *args[] = {"tired", "logs", "relay", "--json", "--lines", "0", "--boot", "current"};
    CHECK(tired_cli_parse(8, args, &request, &error));
    saved = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(saved >= 0);
    directory = mkdtemp(fixture);
    CHECK(directory != NULL && chdir(directory) == 0);
    entered = true;
    CHECK(mkdir("runtime", 0700) == 0 && mkdir("persistent", 0700) == 0);
    output = tmpfile();
    CHECK(output != NULL && session(&request, output, &status, &error) && status == TIRED_OK);
    rewind(output);
    size_t events = 0;
    while (fgets(line, sizeof(line), output) != NULL)
    {
        CHECK(tired_json_parse(line, strlen(line), sizeof(line), &object, &error));
        CHECK(json_object_object_get_ex(object, "selected_service", &value) &&
              strcmp(json_object_get_string(value), "relay.service") == 0);
        CHECK(json_object_object_get_ex(object, "event_type", &value));
        if (strcmp(json_object_get_string(value), "journal_end") == 0)
        {
            CHECK(json_object_object_get_ex(object, "records", &value) &&
                  json_object_get_uint64(value) == 0);
            CHECK(json_object_object_get_ex(object, "exit_code", &value) &&
                  json_object_get_int(value) == 0);
        }
        ++events;
    }
    CHECK(events == 3);
    fclose(output);
    output = NULL;
    fd = open("runtime/system.journal", O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && close(fd) == 0);
    fd = -1;
    output = tmpfile();
    CHECK(output != NULL && session(&request, output, &status, &error) &&
          status == TIRED_RECOVERY_REQUIRED);
    rewind(output);
    CHECK(fgets(line, sizeof(line), output) != NULL);
    CHECK(tired_json_parse(line, strlen(line), sizeof(line), &object, &error));
    CHECK(json_object_object_get_ex(object, "access_checked", &value) &&
          !json_object_get_boolean(value));
    fclose(output);
    output = NULL;
    CHECK(unlink("runtime/system.journal") == 0);
    output = fopen("/dev/full", "w");
    CHECK(output != NULL && !session(&request, output, &status, &error));
    CHECK(strcmp(error.code, "logs-output-write") == 0);
    fclose(output);
    output = NULL;
    CHECK(pipe(pipes) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        close(pipes[0]);
        FILE *stream = fdopen(pipes[1], "w");
        struct sigaction action = {.sa_handler = stop};
        sigemptyset(&action.sa_mask);
        if (stream == NULL || sigaction(SIGTERM, &action, NULL) != 0)
            _exit(2);
        request.logs.follow = true;
        bool ok = session(&request, stream, &status, &error);
        fclose(stream);
        _exit((ok && status == TIRED_INTERRUPTED) || (!ok && error.status == TIRED_INTERRUPTED)
                  ? 0
                  : 3);
    }
    close(pipes[1]);
    pipes[1] = -1;
    struct pollfd poller = {.fd = pipes[0], .events = POLLIN};
    CHECK(poll(&poller, 1, 3000) == 1);
    CHECK(read(pipes[0], line, sizeof(line)) > 0);
    CHECK(kill(child, SIGTERM) == 0);
    int child_status;
    CHECK(waitpid(child, &child_status, 0) == child);
    child = -1;
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    close(pipes[0]);
    pipes[0] = -1;
    CHECK(pipe(pipes) == 0);
    int flags = fcntl(pipes[1], F_GETFL);
    CHECK(flags >= 0 && fcntl(pipes[1], F_SETFL, flags | O_NONBLOCK) == 0);
    memset(line, 'x', sizeof(line));
    while (write(pipes[1], line, sizeof(line)) > 0)
    {
    }
    CHECK(errno == EAGAIN && fcntl(pipes[1], F_SETFL, flags) == 0);
    /* The reader leaves a full pipe untouched while the writer is cancelled. */
    int ready[2];
    CHECK(pipe(ready) == 0);
    child = fork();
    if (child < 0)
    {
        close(ready[0]);
        close(ready[1]);
        CHECK(false);
    }
    if (child == 0)
    {
        close(ready[0]);
        close(pipes[0]);
        struct sigaction action = {.sa_handler = stop};
        sigemptyset(&action.sa_mask);
        FILE *stream = fdopen(pipes[1], "w");
        if (stream == NULL || sigaction(SIGTERM, &action, NULL) != 0)
            _exit(2);
        if (write(ready[1], "r", 1) != 1)
            _exit(2);
        close(ready[1]);
        bool ok = session(&request, stream, &status, &error);
        bool restored = fcntl(pipes[1], F_GETFL) == flags;
        fclose(stream);
        _exit(restored && ((ok && status == TIRED_INTERRUPTED) ||
                           (!ok && error.status == TIRED_INTERRUPTED))
                  ? 0
                  : 3);
    }
    close(ready[1]);
    poller = (struct pollfd){.fd = ready[0], .events = POLLIN};
    int ready_result = poll(&poller, 1, 3000);
    close(ready[0]);
    CHECK(ready_result == 1 && kill(child, SIGTERM) == 0);
    CHECK(waitpid(child, &child_status, 0) == child);
    child = -1;
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    result = 0;
cleanup:
    if (child > 0)
    {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    if (output != NULL)
        fclose(output);
    if (fd >= 0)
        close(fd);
    for (size_t i = 0; i < 2; ++i)
        if (pipes[i] >= 0)
            close(pipes[i]);
    json_object_put(object);
    tired_request_destroy(&request);
    if (entered)
    {
        unlink("runtime/system.journal");
        rmdir("runtime");
        rmdir("persistent");
        (void)fchdir(saved);
    }
    if (directory != NULL)
        rmdir(directory);
    if (saved >= 0)
        close(saved);
    return result;
}
