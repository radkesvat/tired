#define _GNU_SOURCE
#include "tired/helper.h"
#include "tired/json.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(e)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(e))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s [%s]\n", __LINE__, #e,                                         \
                    error.code == NULL ? "none" : error.code);                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    int pipefd[2];
    TiredError error = {0};
    TiredText output = {0};
    (void)signal(SIGPIPE, SIG_IGN);
    /* Frames are independent of write boundaries. A slow sender can split every
     * header byte and payload byte; no frame is accepted before its last byte. */
    CHECK(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        close(pipefd[0]);
        uint32_t header = htonl(7);
        const unsigned char *bytes = (const unsigned char *)&header;
        struct timespec pause = {.tv_nsec = 1000000};
        for (size_t i = 0; i < 4; ++i)
        {
            if (write(pipefd[1], bytes + i, 1) != 1)
                _exit(2);
            nanosleep(&pause, NULL);
        }
        const char payload[] = "{\"a\":1}";
        for (size_t i = 0; i < 7; ++i)
        {
            if (write(pipefd[1], payload + i, 1) != 1)
                _exit(2);
            nanosleep(&pause, NULL);
        }
        _exit(0);
    }
    close(pipefd[1]);
    CHECK(tired_protocol_read(pipefd[0], 1000, &output, &error));
    CHECK(output.length == 7 && memcmp(output.data, "{\"a\":1}", 7) == 0);
    close(pipefd[0]);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    for (unsigned scenario = 0; scenario < 5; ++scenario)
    {
        CHECK(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
        uint32_t header = htonl(scenario == 0 ? 0 : scenario == 1 ? TIRED_INPUT_LIMIT + 1 : 20);
        if (scenario < 3)
        {
            CHECK(write(pipefd[1], &header, 4) == 4);
            close(pipefd[1]);
        }
        volatile sig_atomic_t cancel = scenario == 4;
        CHECK(!tired_protocol_read_interruptible(pipefd[0], 20, &cancel, &output, &error));
        CHECK(output.length == 7);
        CHECK(error.status == (scenario < 2    ? TIRED_INVALID
                               : scenario == 4 ? TIRED_INTERRUPTED
                                               : TIRED_AUTHORIZATION));
        close(pipefd[0]);
        if (scenario >= 3)
            close(pipefd[1]);
    }
    CHECK(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    CHECK(tired_protocol_ready(pipefd[1], &error));
    CHECK(tired_protocol_read(pipefd[0], 100, &output, &error));
    CHECK(strstr(output.data, "\"protocol_version\":1") != NULL);
    close(pipefd[0]);
    CHECK(!tired_protocol_write(pipefd[1], &output, 100, &error));
    CHECK(error.system_errno == EPIPE);
    close(pipefd[1]);
    /* Unknown versions, unsafe UUIDs and mismatched scopes fail before opening a
     * manager or deriving any mutation destinations. */
    const char *invalid[] = {
        "{\"kind\":\"recover\",\"protocol_version\":2,\"user_scope\":true,\"transaction_uuid\":"
        "\"01234567-89ab-4cde-8fab-0123456789ab\",\"finish\":true}",
        "{\"kind\":\"recover\",\"protocol_version\":1,\"user_scope\":false,\"transaction_uuid\":\"."
        "./../foreign\",\"finish\":true}",
        "{\"kind\":\"service_record\",\"protocol_version\":1,\"unit_name\":\"../../"
        "foreign\",\"hydrate\":true}",
        "{\"protocol_version\":1,\"protocol_version\":2}"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        TiredText request = {.data = (char *)invalid[i], .length = strlen(invalid[i])};
        CHECK(!tired_helper_dispatch(&request, true, false, getuid(), &output, &error));
        CHECK(error.status == TIRED_INVALID);
    }
    tired_text_destroy(&output);
    return 0;
}
