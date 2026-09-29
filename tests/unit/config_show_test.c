#include "tired/config_frontend.h"
#include "tired/io.h"
#include "tired/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
int main(void)
{
    int result = 1;
    char fixture[] = "config-show-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    const char *previous = getenv("XDG_CONFIG_HOME");
    char *saved = previous == NULL ? NULL : strdup(previous);
    TiredText root = {0}, directory = {0}, path = {0}, output = {0};
    TiredRequest request = {0};
    TiredSettings settings = {0};
    TiredError error = {0};
    struct json_object *document = NULL, *fields = NULL, *field = NULL, *value = NULL;
    CHECK(cwd != NULL && (previous == NULL || saved != NULL));
    created = mkdtemp(fixture);
    CHECK(created != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &root, &error));
    CHECK(tired_path_absolute(&root, "tired", 5, &directory, &error));
    CHECK(mkdir(directory.data, 0700) == 0);
    CHECK(tired_path_absolute(&directory, "config.json", 11, &path, &error));
    const char *contents =
        "{\"schema_version\":1,\"tui\":true,\"color\":\"always\",\"log_tail\":71}";
    CHECK(tired_write_private_new(path.data, contents, strlen(contents), &error));
    CHECK(setenv("XDG_CONFIG_HOME", root.data, 1) == 0);
    const char *args[] = {"tired", "config", "show", "--json", "--color=never", "--no-tui"};
    CHECK(tired_cli_parse(6, args, &request, &error));
    CHECK(tired_config_discover(&request, &settings, &error));
    CHECK(settings.log_tail == 71 && settings.color == 2 && !settings.tui);
    CHECK(settings.origins[TIRED_SETTING_COLOR] == TIRED_SETTINGS_CLI);
    CHECK(settings.origins[TIRED_SETTING_TUI] == TIRED_SETTINGS_CLI);
    CHECK(settings.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_USER);
    CHECK(tired_config_command(&request, &output, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "settings", &fields));
    CHECK(json_object_object_length(fields) == TIRED_SETTING_COUNT);
    CHECK(json_object_object_get_ex(fields, "log_tail", &field));
    CHECK(json_object_object_get_ex(field, "value", &value) && json_object_get_int(value) == 71);
    CHECK(json_object_object_get_ex(field, "origin", &value));
    CHECK(strcmp(json_object_get_string(value), "user") == 0);
    request.json = false;
    CHECK(tired_config_command(&request, &output, &error));
    CHECK(strstr(output.data, "color = \"never\" [cli]") != NULL);
    CHECK(strstr(output.data, "log_tail = 71 [user]") != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", "relative", 1) == 0);
    CHECK(!tired_config_discover(&request, &settings, &error));
    CHECK(settings.log_tail == 71);
    CHECK(setenv("XDG_CONFIG_HOME", root.data, 1) == 0);
    CHECK(unlink(path.data) == 0);
    CHECK(tired_write_private_new(path.data, "{}", 2, &error));
    CHECK(!tired_config_command(&request, &output, &error));
    CHECK(strstr(output.data, "log_tail = 71 [user]") != NULL);
    result = 0;
cleanup:
    if (previous == NULL)
        (void)unsetenv("XDG_CONFIG_HOME");
    else if (saved != NULL)
        (void)setenv("XDG_CONFIG_HOME", saved, 1);
    if (created != NULL)
    {
        if (path.data != NULL)
            (void)unlink(path.data);
        if (directory.data != NULL)
            (void)rmdir(directory.data);
        (void)rmdir(created);
    }
    free(saved);
    free(cwd);
    tired_text_destroy(&root);
    tired_text_destroy(&directory);
    tired_text_destroy(&path);
    tired_text_destroy(&output);
    tired_request_destroy(&request);
    tired_settings_destroy(&settings);
    json_object_put(document);
    return result;
}
