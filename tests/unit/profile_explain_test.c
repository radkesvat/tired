#include "tired/io.h"
#include "tired/profile_frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
int main(int argc, char **argv)
{
    TiredError error = {0};
    if (argc >= 3 && strcmp(argv[1], "--workload") == 0)
        return tired_write_private_new(argv[2], "executed", 8, &error) ? 0 : 1;
    int result = 1;
    char fixture[] = "/tmp/tired-explain-XXXXXX";
    char *root = mkdtemp(fixture);
    char marker[128] = {0};
    TiredRequest request = {0};
    TiredText output = {0};
    CHECK(argc == 2 && root != NULL);
    CHECK(snprintf(marker, sizeof(marker), "%s/executed", root) > 0);
    const char *args[] = {"tired",
                          "profiles",
                          "explain",
                          "--profile",
                          "backhaul",
                          "--json",
                          "--restart",
                          "no",
                          "--",
                          argv[0],
                          "--workload",
                          marker,
                          "--token=secret-fixture"};
    CHECK(tired_cli_parse(13, args, &request, &error));
    CHECK(tired_profiles_command(&request, argv[1], &output, &error));
    CHECK(strstr(output.data, "profiles explain") != NULL);
    CHECK(strstr(output.data, "user-override") != NULL);
    CHECK(strstr(output.data, "condition-unknown") != NULL);
    CHECK(strstr(output.data, "profile_digest") != NULL);
    CHECK(strstr(output.data, "source_ids") != NULL);
    CHECK(strstr(output.data, "secret-fixture") == NULL);
    CHECK(access(marker, F_OK) != 0);
    request.json = false;
    CHECK(tired_profiles_command(&request, argv[1], &output, &error));
    CHECK(strstr(output.data, "Passive evaluation; target not executed") != NULL);
    CHECK(strstr(output.data, "Evidence:") != NULL);
    CHECK(access(marker, F_OK) != 0);
    CHECK(tired_text_set(&request.profile, "none", 4, 128, &error));
    CHECK(tired_profiles_command(&request, "/unavailable/bundle", &output, &error));
    CHECK(strstr(output.data, "matching disabled") != NULL);
    result = 0;
cleanup:
    if (root != NULL)
    {
        (void)unlink(marker);
        (void)rmdir(root);
    }
    tired_request_destroy(&request);
    tired_text_destroy(&output);
    return result;
}
