#include "tired/encode.h"
#include "tired/io.h"
#include "tired/verify.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static bool finish(TiredVerification *verification)
{
    for (unsigned i = 0; i < 10000; ++i)
    {
        if (tired_verify_step(verification))
            return true;
        struct timespec delay = {.tv_nsec = 1000000};
        (void)nanosleep(&delay, NULL);
    }
    return false;
}
int main(int argc, char **argv)
{
    TiredError error = {0};
    if (argc == 3 && strcmp(argv[1], "--workload") == 0)
        return tired_write_private_new(argv[2], "executed", 8, &error) ? 0 : 1;
    if (access("/usr/bin/systemd-analyze", F_OK) != 0 && errno == ENOENT)
        return 77;
    int result = 1;
    char fixture[] = "/tmp/tired-verifier-test-XXXXXX";
    char *root = mkdtemp(fixture);
    TiredVerification *verification = NULL;
    TiredText command = {0}, marker = {0}, token = {0}, unit = {0};
    TiredText name = {.data = "tired-verifier-fixture", .length = 22};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_UNIT_LIMIT);
    CHECK(root != NULL);
    TiredText directory = {.data = root, .length = strlen(root)};
    CHECK(tired_path_absolute(&directory, "executed", 8, &marker, &error));
    CHECK(tired_encode_token(argv[0], strlen(argv[0]), &command, &error));
    CHECK(tired_encode_token(marker.data, marker.length, &token, &error));
    const char *prefix = "[Unit]\nDefaultDependencies=no\n[Service]\nType=oneshot\nExecStart=:";
    CHECK(tired_buffer_append(&buffer, prefix, strlen(prefix), &error));
    CHECK(tired_buffer_append(&buffer, command.data, command.length, &error));
    CHECK(tired_buffer_append(&buffer, " --workload ", 12, &error));
    CHECK(tired_buffer_append(&buffer, token.data, token.length, &error));
    CHECK(tired_buffer_append(&buffer, "\n", 1, &error));
    CHECK(tired_buffer_take(&buffer, &unit, &error));
    CHECK(tired_verify_start(&name, unit.data, unit.length, NULL, 5000, &verification, &error));
    CHECK(finish(verification));
    TiredVerifyResult observed = tired_verify_result(verification);
    CHECK(observed.process.outcome == TIRED_PROCESS_EXITED && observed.process.exit_code == 0);
    CHECK(observed.state == TIRED_VERIFY_CLEAN || observed.state == TIRED_VERIFY_DIAGNOSTICS);
    CHECK(observed.cleanup_complete && observed.retained_directory == NULL);
    CHECK(access(marker.data, F_OK) != 0);
    tired_verify_destroy(verification);
    verification = NULL;
    const char *invalid = "[Service]\nExecStart=/no/such/tired-workload\n";
    CHECK(tired_verify_start(&name, invalid, strlen(invalid), NULL, 5000, &verification, &error));
    CHECK(finish(verification));
    observed = tired_verify_result(verification);
    CHECK(observed.state == TIRED_VERIFY_NONZERO && observed.process.error_length != 0);
    CHECK(observed.cleanup_complete);
    tired_verify_destroy(verification);
    verification = NULL;
    const char *warning =
        "[Service]\nType=oneshot\nExecStart=/usr/bin/true\nTiredUnknownOption=yes\n";
    CHECK(tired_verify_start(&name, warning, strlen(warning), NULL, 5000, &verification, &error));
    CHECK(finish(verification));
    observed = tired_verify_result(verification);
    CHECK(observed.state == TIRED_VERIFY_DIAGNOSTICS || observed.state == TIRED_VERIFY_NONZERO);
    CHECK(observed.process.error_length != 0 && observed.cleanup_complete);
    tired_verify_destroy(verification);
    verification = NULL;
    CHECK(tired_verify_start(&name, unit.data, unit.length, NULL, 5000, &verification, &error));
    tired_verify_cancel(verification);
    CHECK(finish(verification));
    observed = tired_verify_result(verification);
    CHECK(observed.state == TIRED_VERIFY_INCOMPLETE);
    CHECK(observed.process.outcome == TIRED_PROCESS_CANCELLED && observed.cleanup_complete);
    tired_verify_destroy(verification);
    verification = NULL;
    CHECK(!tired_verify_start(&name, unit.data, unit.length, NULL, 0, &verification, &error));
    CHECK(verification != NULL && tired_verify_result(verification).cleanup_complete);
    tired_verify_destroy(verification);
    verification = NULL;
    TiredVerifyUserPaths missing = {0};
    CHECK(
        !tired_verify_start(&name, unit.data, unit.length, &missing, 5000, &verification, &error));
    CHECK(strcmp(error.code, "verify-user-path") == 0);
    result = 0;
cleanup:
    if (verification != NULL)
    {
        tired_verify_cancel(verification);
        if (finish(verification))
        {
            if (!tired_verify_result(verification).cleanup_complete)
                result = 1;
            tired_verify_destroy(verification);
        }
    }
    if (marker.data != NULL)
        (void)unlink(marker.data);
    if (root != NULL)
        (void)rmdir(root);
    tired_text_destroy(&command);
    tired_text_destroy(&marker);
    tired_text_destroy(&token);
    tired_text_destroy(&unit);
    tired_buffer_destroy(&buffer);
    return result;
}
