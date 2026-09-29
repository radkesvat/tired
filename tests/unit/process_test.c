#include "tired/process.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static bool finish(TiredProcess *process)
{
    for (unsigned i = 0; i < 5000; ++i)
    {
        if (tired_process_step(process))
            return true;
        struct timespec delay = {.tv_nsec = 1000000};
        (void)nanosleep(&delay, NULL);
    }
    return false;
}
int main(int argc, char **argv)
{
    if (argc > 1)
    {
        if (strcmp(argv[1], "closed") == 0)
        {
            (void)close(0);
            (void)close(1);
            (void)close(2);
            char *nested_args[] = {argv[0], "capture", "literal", NULL};
            char *nested_environment[] = {"ONLY=controlled", NULL};
            TiredProcess *nested = NULL;
            TiredError nested_error = {0};
            if (!tired_process_start(argv[0], nested_args, nested_environment, 3000, 1024, &nested,
                                     &nested_error) ||
                !finish(nested))
                return 96;
            TiredProcessResult nested_result = tired_process_result(nested);
            bool ok = nested_result.outcome == TIRED_PROCESS_EXITED && nested_result.exit_code == 7;
            tired_process_destroy(nested);
            return ok ? 0 : 97;
        }
        if (strcmp(argv[1], "capture") == 0)
        {
            const char *value = getenv("ONLY");
            if (value == NULL || getenv("TIRED_PROCESS_PARENT") != NULL)
                return 90;
            if (fcntl(100, F_GETFD) != -1 || errno != EBADF)
                return 91;
            char byte;
            if (read(0, &byte, 1) != 0)
                return 92;
            (void)write(1, value, strlen(value));
            (void)write(1, argv[2], strlen(argv[2]));
            (void)write(2, "err\0tail", 8);
            return 7;
        }
        if (strcmp(argv[1], "flood") == 0)
        {
            char data[4096];
            memset(data, 'x', sizeof(data));
            for (unsigned i = 0; i < 64; ++i)
            {
                if (write(1, data, sizeof(data)) < 0 || write(2, data, sizeof(data)) < 0)
                    return 93;
            }
            return 0;
        }
        if (strcmp(argv[1], "signal") == 0)
        {
            (void)raise(SIGTERM);
            return 94;
        }
        if (strcmp(argv[1], "descendant") == 0)
        {
            pid_t child = fork();
            if (child < 0)
                return 95;
            if (child > 0)
                return 0;
        }
        for (;;)
            pause();
    }
    TiredProcess *process = NULL;
    CHECK(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
    TiredError error = {0};
    char *environment[] = {"ONLY=controlled", NULL};
    char *args[] = {argv[0], "capture", "$literal ; argument", NULL};
    CHECK(setenv("TIRED_PROCESS_PARENT", "must-not-leak", 1) == 0);
    int extra = open("/dev/null", O_RDONLY);
    CHECK(extra >= 0 && dup2(extra, 100) == 100);
    CHECK(tired_process_start(argv[0], args, environment, 3000, 1024, &process, &error));
    CHECK(finish(process));
    TiredProcessResult result = tired_process_result(process);
    CHECK(result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 7);
    CHECK(result.output_length == strlen("controlled$literal ; argument"));
    CHECK(memcmp(result.standard_output, "controlled$literal ; argument", result.output_length) ==
          0);
    CHECK(result.error_length == 8 && memcmp(result.standard_error, "err\0tail", 8) == 0);
    tired_process_destroy(process);
    process = NULL;
    CHECK(close(extra) == 0 && close(100) == 0);
    args[1] = "flood";
    CHECK(
        tired_process_start(argv[0], args, environment, 3000, TIRED_INPUT_LIMIT, &process, &error));
    CHECK(finish(process));
    result = tired_process_result(process);
    CHECK(result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 0);
    CHECK(result.output_length == 262144 && result.error_length == 262144);
    tired_process_destroy(process);
    process = NULL;
    CHECK(tired_process_start(argv[0], args, environment, 3000, 100, &process, &error));
    CHECK(finish(process));
    CHECK(tired_process_result(process).outcome == TIRED_PROCESS_OUTPUT_LIMIT);
    tired_process_destroy(process);
    process = NULL;
    args[1] = "sleep";
    CHECK(tired_process_start(argv[0], args, environment, 30, 1024, &process, &error));
    CHECK(finish(process));
    CHECK(tired_process_result(process).outcome == TIRED_PROCESS_TIMEOUT);
    tired_process_destroy(process);
    process = NULL;
    CHECK(tired_process_start(argv[0], args, environment, 3000, 1024, &process, &error));
    tired_process_cancel(process);
    CHECK(finish(process));
    CHECK(tired_process_result(process).outcome == TIRED_PROCESS_CANCELLED);
    tired_process_destroy(process);
    process = NULL;
    args[1] = "signal";
    CHECK(tired_process_start(argv[0], args, environment, 3000, 1024, &process, &error));
    CHECK(finish(process));
    result = tired_process_result(process);
    CHECK(result.outcome == TIRED_PROCESS_SIGNALED && result.signal_number == SIGTERM);
    tired_process_destroy(process);
    process = NULL;
    args[1] = "descendant";
    CHECK(tired_process_start(argv[0], args, environment, 30, 1024, &process, &error));
    CHECK(finish(process));
    CHECK(tired_process_result(process).outcome == TIRED_PROCESS_TIMEOUT);
    tired_process_destroy(process);
    process = NULL;
    args[1] = "closed";
    CHECK(tired_process_start(argv[0], args, environment, 3000, 1024, &process, &error));
    CHECK(finish(process));
    result = tired_process_result(process);
    CHECK(result.outcome == TIRED_PROCESS_EXITED && result.exit_code == 0);
    tired_process_destroy(process);
    process = NULL;
    CHECK(
        !tired_process_start("/no/such/tired-tool", args, environment, 100, 100, &process, &error));
    CHECK(process == NULL);
    CHECK(!tired_process_start("relative", args, environment, 100, 100, &process, &error));
    /* Reap the deliberately orphaned fixture descendant after group cancellation. */
    bool drained = false;
    for (unsigned i = 0; i < 5000; ++i)
    {
        pid_t child = waitpid(-1, NULL, WNOHANG);
        if (child < 0 && errno == ECHILD)
        {
            drained = true;
            break;
        }
        struct timespec delay = {.tv_nsec = 1000000};
        (void)nanosleep(&delay, NULL);
    }
    CHECK(drained);
    return 0;
}
