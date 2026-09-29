#include "tired/identity.h"
#include "tired/layout.h"
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
int main(void)
{
    int result = 1;
    TiredLayout layout = {0};
    TiredAccount account = {0};
    TiredError error = {0};
    TiredTextList paths = {0};
    CHECK(tired_layout_resolve(false, "bad", "bad", "bad", "bad", &layout, &error));
    CHECK(!layout.user_scope);
    const char *expected[] = {"/etc/tired/config.json",      "/etc/tired/profiles.d",
                              "/etc/tired/services",         "/etc/systemd/system",
                              "/var/lib/tired/services",     "/var/lib/tired/history",
                              "/var/lib/tired/transactions", "/run/tired/operation.lock"};
    for (size_t i = 0; i < TIRED_PATH_COUNT; ++i)
        CHECK(strcmp(layout.paths[i].data, expected[i]) == 0);
    CHECK(tired_layout_check_unit_path(&layout, &paths, &error));
    CHECK(
        tired_layout_resolve(true, "/home/alice/", NULL, "", "/run/user/123///", &layout, &error));
    CHECK(layout.user_scope);
    CHECK(strcmp(layout.paths[TIRED_PATH_CONFIG].data, "/home/alice/.config/tired/config.json") ==
          0);
    CHECK(strcmp(layout.paths[TIRED_PATH_RECORDS].data,
                 "/home/alice/.local/state/tired/services") == 0);
    CHECK(strcmp(layout.paths[TIRED_PATH_UNITS].data, "/home/alice/.config/systemd/user") == 0);
    CHECK(strcmp(layout.paths[TIRED_PATH_OPERATION_LOCK].data,
                 "/run/user/123/tired/operation.lock") == 0);
    CHECK(!tired_layout_check_unit_path(&layout, &paths, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(
        tired_text_list_append(&paths, "/other/systemd/user", 19, 256, TIRED_INPUT_LIMIT, &error));
    CHECK(!tired_layout_check_unit_path(&layout, &paths, &error));
    const char *match = "/home/alice/.config/systemd/user/";
    CHECK(tired_text_list_append(&paths, match, strlen(match), 256, TIRED_INPUT_LIMIT, &error));
    CHECK(tired_layout_check_unit_path(&layout, &paths, &error));
    const char *bad[] = {"relative", "/home/../escape", "/home/./alice", "/home//alice",
                         "/bad\npath"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(!tired_layout_resolve(true, "/home/alice", bad[i], NULL, "/run/user/123", &layout,
                                    &error));
        CHECK(!tired_layout_resolve(true, "/home/alice", NULL, bad[i], "/run/user/123", &layout,
                                    &error));
        CHECK(!tired_layout_resolve(true, "/home/alice", NULL, NULL, bad[i], &layout, &error));
        CHECK(strcmp(layout.paths[TIRED_PATH_CONFIG].data,
                     "/home/alice/.config/tired/config.json") == 0);
    }
    CHECK(!tired_layout_resolve(true, "/home/alice", NULL, NULL, NULL, &layout, &error));
    CHECK(error.status == TIRED_NOT_FOUND);
    CHECK(!tired_layout_resolve(true, NULL, NULL, NULL, "/run/user/123", &layout, &error));
    CHECK(tired_layout_resolve(true, NULL, "/config", "/state", "/runtime", &layout, &error));
    CHECK(strcmp(layout.paths[TIRED_PATH_ENVIRONMENT_SERVICES].data, "/config/tired/services") ==
          0);
    CHECK(strcmp(layout.paths[TIRED_PATH_HISTORY].data, "/state/tired/history") == 0);
    CHECK(strcmp(layout.paths[TIRED_PATH_TRANSACTIONS].data, "/state/tired/transactions") == 0);
    CHECK(tired_layout_resolve(true, "/", NULL, NULL, "/", &layout, &error));
    CHECK(strcmp(layout.paths[TIRED_PATH_CONFIG].data, "/.config/tired/config.json") == 0);
    CHECK(strcmp(layout.paths[TIRED_PATH_OPERATION_LOCK].data, "/tired/operation.lock") == 0);
    char long_path[4098];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';
    CHECK(!tired_layout_resolve(true, NULL, long_path, "/state", "/runtime", &layout, &error));
    long_path[257] = '\0';
    CHECK(!tired_layout_resolve(true, NULL, long_path, "/state", "/runtime", &layout, &error));
    /* Discovery must not derive administrative paths from any user environment. */
    CHECK(setenv("HOME", "relative-untrusted-home", 1) == 0);
    CHECK(setenv("XDG_CONFIG_HOME", "bad", 1) == 0);
    CHECK(setenv("XDG_STATE_HOME", "bad", 1) == 0);
    CHECK(setenv("XDG_RUNTIME_DIR", "bad", 1) == 0);
    CHECK(tired_layout_discover(false, &layout, &error));
    CHECK(strcmp(layout.paths[TIRED_PATH_RECORDS].data, "/var/lib/tired/services") == 0);
    CHECK(!tired_layout_discover(true, &layout, &error));
    CHECK(unsetenv("XDG_CONFIG_HOME") == 0 && unsetenv("XDG_STATE_HOME") == 0);
    CHECK(setenv("XDG_RUNTIME_DIR", "/run/user/123", 1) == 0);
    CHECK(tired_account_by_uid(getuid(), &account, &error));
    CHECK(tired_layout_discover(true, &layout, &error));
    CHECK(strncmp(layout.paths[TIRED_PATH_CONFIG].data, account.home.data, account.home.length) ==
          0);
    result = 0;
cleanup:
    tired_layout_destroy(&layout);
    tired_text_list_destroy(&paths);
    tired_account_destroy(&account);
    return result;
}
