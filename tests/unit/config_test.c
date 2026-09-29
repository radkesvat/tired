#include "tired/config_frontend.h"
#include "tired/io.h"
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

static bool command(TiredRequest *request, TiredText *output, TiredError *error, const char *path,
                    bool user, bool json)
{
    const char *args[] = {"tired",
                          "config",
                          "validate",
                          path,
                          user ? "--user" : "--system",
                          json ? "--json" : "--no-tui"};
    return tired_cli_parse(6, args, request, error) && tired_config_command(request, output, error);
}
int main(void)
{
    int result = 1;
    char directory[] = "/tmp/tired-config-XXXXXX";
    char path[128] = {0};
    char *root = mkdtemp(directory);
    TiredRequest request = {0};
    TiredText output = {0}, original = {0};
    TiredError error = {0};
    CHECK(root != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/settings.json", root) > 0);
    const char *valid =
        "{\"schema_version\":1,\"profile_directories\":[\"/not/a/real/directory\"]}";
    CHECK(tired_write_private_new(path, valid, strlen(valid), &error));
    CHECK(command(&request, &output, &error, path, false, true));
    CHECK(strstr(output.data, "\"trust_checked\":false") != NULL);
    CHECK(strstr(output.data, "\"base\":\"built_in_defaults\"") != NULL);
    CHECK(tired_text_set(&original, output.data, output.length, TIRED_INPUT_LIMIT, &error));
    CHECK(!command(&request, &output, &error, path, true, true));
    CHECK(strcmp(error.code, "settings-trust") == 0);
    CHECK(strcmp(output.data, original.data) == 0);
    CHECK(unlink(path) == 0);
    const char *zero = "{\"schema_version\":1,\"restart_sec\":\"0s\"}";
    CHECK(tired_write_private_new(path, zero, strlen(zero), &error));
    CHECK(!command(&request, &output, &error, path, false, true));
    CHECK(strcmp(error.code, "settings-retry") == 0);
    CHECK(unlink(path) == 0);
    const char *bad = "{\"schema_version\":1,\"tui\":true,\"tui\":false}";
    CHECK(tired_write_private_new(path, bad, strlen(bad), &error));
    CHECK(!command(&request, &output, &error, path, false, true));
    CHECK(unlink(path) == 0);
    CHECK(!command(&request, &output, &error, path, false, true));
    valid = "{\"schema_version\":1,\"ascii\":true}";
    CHECK(tired_write_private_new(path, valid, strlen(valid), &error));
    CHECK(command(&request, &output, &error, path, true, false));
    CHECK(strstr(output.data, "Valid settings") != NULL);
    CHECK(tired_read_file(path, TIRED_INPUT_LIMIT, &original, &error));
    CHECK(strcmp(original.data, valid) == 0);
    const char *extra[] = {"tired", "config", "validate", path, "extra"};
    CHECK(tired_cli_parse(5, extra, &request, &error));
    CHECK(!tired_config_command(&request, &output, &error));
    CHECK(error.status == TIRED_INVALID);
    result = 0;
cleanup:
    tired_request_destroy(&request);
    tired_text_destroy(&output);
    tired_text_destroy(&original);
    if (root != NULL)
    {
        (void)unlink(path);
        (void)rmdir(root);
    }
    return result;
}
