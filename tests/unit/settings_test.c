#include "tired/settings.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    TiredSettings defaults = {0}, input = {0};
    TiredError error = {0};
    tired_settings_defaults(&defaults);
    CHECK(defaults.history_revisions == 5);
    CHECK(defaults.tui && defaults.restart_usec == 5000000 && defaults.observation_usec == 3000000);
    const char *valid = "{\"schema_version\":1,\"color\":\"never\",\"log_tail\":500,\"retry_"
                        "policy\":\"limited\",\"history_revisions\":9,\"profile_directories\":[\"/"
                        "etc/site/profiles\"]}";
    CHECK(tired_settings_parse(valid, strlen(valid), &input, &error));
    CHECK(!tired_settings_merge(&defaults, &input, TIRED_SETTINGS_USER, &error));
    CHECK(defaults.log_tail == 200);
    CHECK(tired_settings_merge(&defaults, &input, TIRED_SETTINGS_ADMIN, &error));
    CHECK(defaults.color == 2 && defaults.log_tail == 500 && defaults.limited_retries);
    CHECK(defaults.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_ADMIN);
    const char *user = "{\"schema_version\":1,\"ascii\":true,\"log_tail\":20}";
    CHECK(tired_settings_parse(user, strlen(user), &input, &error));
    CHECK(tired_settings_merge(&defaults, &input, TIRED_SETTINGS_USER, &error));
    CHECK(defaults.ascii && defaults.log_tail == 20 && defaults.profile_directories.count == 1);
    CHECK(defaults.origins[TIRED_SETTING_LOG_TAIL] == TIRED_SETTINGS_USER);
    CHECK(defaults.history_revisions == 9 &&
          defaults.origins[TIRED_SETTING_HISTORY] == TIRED_SETTINGS_ADMIN);
    const char *bad[] = {
        "{}",
        "{\"schema_version\":2}",
        "{\"schema_version\":1,\"state_root\":\"/tmp\"}",
        "{\"schema_version\":1,\"log_tail\":1.5}",
        "{\"schema_version\":1,\"observation_sec\":\"0s\"}",
        "{\"schema_version\":1,\"retry_policy\":\"persistent\",\"restart_sec\":\"0\"}",
        "{\"schema_version\":1,\"profile_directories\":[\"relative\"]}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!tired_settings_parse(bad[i], strlen(bad[i]), &input, &error));
    CHECK(input.ascii && input.log_tail == 20);
    const char *zero_delay = "{\"schema_version\":1,\"restart_sec\":\"0\"}";
    CHECK(tired_settings_parse(zero_delay, strlen(zero_delay), &input, &error));
    CHECK(tired_settings_merge(&defaults, &input, TIRED_SETTINGS_USER, &error));
    CHECK(defaults.limited_retries && defaults.restart_usec == 0);
    CHECK(defaults.origins[TIRED_SETTING_RESTART_DELAY] == TIRED_SETTINGS_USER);
    const char *persistent = "{\"schema_version\":1,\"retry_policy\":\"persistent\"}";
    CHECK(tired_settings_parse(persistent, strlen(persistent), &input, &error));
    CHECK(!tired_settings_merge(&defaults, &input, TIRED_SETTINGS_USER, &error));
    CHECK(defaults.limited_retries && defaults.restart_usec == 0);
    CHECK(defaults.profile_directories.count == 1);
    tired_settings_defaults(&defaults);
    CHECK(tired_settings_parse(zero_delay, strlen(zero_delay), &input, &error));
    CHECK(!tired_settings_merge(&defaults, &input, TIRED_SETTINGS_USER, &error));
    CHECK(defaults.restart_usec == 5000000);
    tired_settings_destroy(&defaults);
    tired_settings_destroy(&input);
    return 0;
}
