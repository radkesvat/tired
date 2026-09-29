#include "tired/io.h"
#include "tired/settings.h"
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
    char fixture[] = "settings-file-XXXXXX";
    char *created = NULL, *cwd = getcwd(NULL, 0);
    TiredText directory = {0}, path = {0}, second = {0}, malformed = {0};
    TiredSettings settings = {0};
    TiredError error = {0};
    CHECK(cwd != NULL);
    created = mkdtemp(fixture);
    CHECK(created != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &directory, &error));
    CHECK(tired_path_absolute(&directory, "settings.json", 13, &path, &error));
    CHECK(tired_path_absolute(&directory, "second.json", 11, &second, &error));
    CHECK(tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(settings.log_tail == 200 &&
          settings.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_DEFAULT);
    const char *valid = "{\"schema_version\":1,\"log_tail\":17}";
    CHECK(tired_write_private_new(path.data, valid, strlen(valid), &error));
    CHECK(tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(settings.log_tail == 17 &&
          settings.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_USER);
    CHECK(chmod(path.data, 0666) == 0);
    CHECK(!tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(settings.log_tail == 17);
    CHECK(chmod(path.data, 0600) == 0);
    CHECK(chmod(directory.data, 0777) == 0);
    CHECK(!tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(chmod(directory.data, 0700) == 0);
    CHECK(link(path.data, second.data) == 0);
    CHECK(!tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(unlink(second.data) == 0);
    CHECK(symlink(path.data, second.data) == 0);
    CHECK(!tired_settings_load(NULL, second.data, getuid(), &settings, &error));
    CHECK(unlink(path.data) == 0);
    CHECK(!tired_settings_load(NULL, second.data, getuid(), &settings, &error));
    CHECK(unlink(second.data) == 0);
    CHECK(mkfifo(path.data, 0600) == 0);
    CHECK(!tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(unlink(path.data) == 0);
    CHECK(tired_write_private_new(path.data, "{}", 2, &error));
    CHECK(!tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    CHECK(settings.log_tail == 17);
    CHECK(unlink(path.data) == 0);
    CHECK(tired_path_absolute(&directory, "missing/../bad", 14, &malformed, &error));
    CHECK(!tired_settings_load(NULL, malformed.data, getuid(), &settings, &error));
    CHECK(strcmp(error.code, "settings-path") == 0);
    CHECK(tired_write_private_new(path.data, valid, strlen(valid), &error));
    if (getuid() == 0)
    {
        const char *user = "{\"schema_version\":1,\"ascii\":true}";
        CHECK(tired_write_private_new(second.data, user, strlen(user), &error));
        CHECK(tired_settings_load(path.data, second.data, getuid(), &settings, &error));
        CHECK(settings.ascii && settings.log_tail == 17);
        CHECK(settings.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_ADMIN);
        CHECK(settings.origins[TIRED_SETTING_ASCII] == TIRED_SETTINGS_USER);
        CHECK(unlink(second.data) == 0);
        CHECK(tired_write_private_new(second.data, "{}", 2, &error));
        CHECK(!tired_settings_load(path.data, second.data, getuid(), &settings, &error));
        CHECK(settings.ascii && settings.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_ADMIN);
        CHECK(unlink(second.data) == 0);
        CHECK(chown(path.data, 65534, (gid_t)-1) == 0);
        CHECK(!tired_settings_load(path.data, NULL, getuid(), &settings, &error));
        CHECK(!tired_settings_load(NULL, path.data, getuid(), &settings, &error));
    }
    else
        CHECK(!tired_settings_load(path.data, NULL, getuid(), &settings, &error));
    CHECK(tired_settings_load(NULL, NULL, getuid(), &settings, &error));
    CHECK(settings.log_tail == 200 && !settings.ascii);
    result = 0;
cleanup:
    if (created != NULL)
    {
        if (path.data != NULL)
            (void)unlink(path.data);
        if (second.data != NULL)
            (void)unlink(second.data);
        (void)rmdir(created);
    }
    free(cwd);
    tired_text_destroy(&directory);
    tired_text_destroy(&path);
    tired_text_destroy(&second);
    tired_text_destroy(&malformed);
    tired_settings_destroy(&settings);
    return result;
}
