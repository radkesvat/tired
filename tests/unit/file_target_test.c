#include "tired/file_target.h"
#include "tired/io.h"
#include <stdio.h>
#include <string.h>
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
    TiredResolvedFile resolved = {0};
    TiredError error = {0};
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_UNIT,
                              .service_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
                              .unit_name = {.data = "relay.service", .length = 13}};
    CHECK(tired_layout_resolve(false, "bad", "bad", "bad", "bad", &layout, &error));
    CHECK(tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data, "/etc/systemd/system") == 0);
    CHECK(strcmp(resolved.name.data, "relay.service") == 0 && resolved.mode == 0644 &&
          !resolved.private_directory);
    target.unit_name = (TiredText){.data = "../relay.service", .length = 16};
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.name.data, "relay.service") == 0);
    target.unit_name = (TiredText){0};
    target.role = TIRED_FILE_TARGET_RECORD;
    CHECK(tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data, "/var/lib/tired/services") == 0);
    CHECK(strcmp(resolved.name.data, "01234567-89ab-4cde-8fab-0123456789ab.json") == 0);
    CHECK(resolved.mode == 0600 && resolved.private_directory);
    target.role = TIRED_FILE_TARGET_ENVIRONMENT;
    memcpy(target.revision_uuid, "fedcba98-7654-4321-abcd-fedcba987654", 37);
    CHECK(tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data,
                 "/etc/tired/services/01234567-89ab-4cde-8fab-0123456789ab/"
                 "revisions/fedcba98-7654-4321-abcd-fedcba987654") == 0);
    CHECK(strcmp(resolved.name.data, "environment") == 0 && resolved.mode == 0600 &&
          resolved.private_directory);
    CHECK(tired_layout_resolve(true, "/home/alice", NULL, NULL, "/run/user/123", &layout, &error));
    CHECK(tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strncmp(resolved.directory.data, "/home/alice/.config/tired/services/",
                  sizeof("/home/alice/.config/tired/services/") - 1) == 0);
    target.revision_uuid[14] = '1';
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    target.revision_uuid[14] = '4';
    target.unit_name = (TiredText){.data = "relay.service", .length = 13};
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    target.role = TIRED_FILE_TARGET_UNIT;
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    target.revision_uuid[0] = '\0';
    CHECK(tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data, "/home/alice/.config/systemd/user") == 0);
    target.role = TIRED_FILE_TARGET_RECORD;
    target.unit_name = (TiredText){0};
    CHECK(tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data, "/home/alice/.local/state/tired/services") == 0);
    target.service_uuid[9] = 'A';
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data, "/home/alice/.local/state/tired/services") == 0);
    target.service_uuid[9] = '8';
    target.role = (TiredFileTargetRole)-1;
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    char long_config[4022];
    for (size_t i = 0; i < sizeof(long_config) - 1; ++i)
        long_config[i] = i % 2 == 0 ? '/' : 'a';
    long_config[sizeof(long_config) - 1] = '\0';
    CHECK(tired_layout_resolve(true, "/home/alice", long_config, "/state", "/runtime", &layout,
                               &error));
    target.role = TIRED_FILE_TARGET_ENVIRONMENT;
    memcpy(target.revision_uuid, "fedcba98-7654-4321-abcd-fedcba987654", 37);
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(resolved.directory.data, "/home/alice/.local/state/tired/services") == 0);
    char uuid[37];
    char long_unit[89];
    memset(long_unit, 'a', 80);
    memcpy(long_unit + 80, ".service", 9);
    target.role = TIRED_FILE_TARGET_UNIT;
    target.revision_uuid[0] = '\0';
    target.unit_name = (TiredText){.data = long_unit, .length = 88};
    CHECK(!tired_file_target_resolve(&layout, &target, &resolved, &error));
    CHECK(strcmp(error.code, "file-target-path-limit") == 0);
    CHECK(strcmp(resolved.directory.data, "/home/alice/.local/state/tired/services") == 0);
    CHECK(tired_uuid_create(uuid, &error) && tired_uuid_valid(uuid, 36));
    CHECK(!tired_uuid_valid(uuid, 35) && !tired_uuid_valid(NULL, 0));
    uuid[19] = '7';
    CHECK(!tired_uuid_valid(uuid, 36));
    result = 0;
cleanup:
    tired_resolved_file_destroy(&resolved);
    tired_layout_destroy(&layout);
    return result;
}
