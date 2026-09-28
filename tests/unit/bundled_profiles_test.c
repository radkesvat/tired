#include "tired/io.h"
#include "tired/profile_merge.h"
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
int main(int argc, char **argv)
{
    CHECK(argc == 5);
    for (int index = 1; index < argc; ++index)
    {
        TiredText file = {0}, executable = {0};
        TiredProfile profile = {0};
        TiredServiceSpec spec = {0};
        TiredProfileMerge merged = {0};
        TiredError error = {0};
        TiredProfileContext context = {.systemd_version = 249};
        CHECK(tired_read_file(argv[index], TIRED_PROFILE_LIMIT, &file, &error));
        CHECK(tired_profile_parse(file.data, file.length, &profile, &error));
        CHECK(tired_text_set(&executable, profile.id, strlen(profile.id), 128, &error));
        CHECK(tired_profile_matches(&profile, &executable) == (strcmp(profile.id, "generic") != 0));
        CHECK(tired_text_set(&executable, "frp", 3, 128, &error));
        CHECK(!tired_profile_matches(&profile, &executable));
        CHECK(tired_text_set(&executable, "unknown-program", 15, 128, &error));
        CHECK(!tired_profile_matches(&profile, &executable));
        CHECK(tired_spec_defaults(&spec, &error));
        const char *args[] = {profile.id, "-c", "relative config.toml"};
        for (size_t i = 0; i < 3; ++i)
            CHECK(tired_spec_append(&spec, TIRED_FIELD_ARGV, args[i], strlen(args[i]),
                                    TIRED_ORIGIN_CAPTURE, &error));
        CHECK(tired_profile_merge(&profile, &spec, &context, &merged, &error));
        CHECK(merged.spec.fields[TIRED_FIELD_ARGV].value.list.count == 3);
        CHECK(strcmp(merged.spec.fields[TIRED_FIELD_ARGV].value.list.items[2].data, args[2]) == 0);
        CHECK(merged.spec.fields[TIRED_FIELD_RUN_AS].origin == TIRED_ORIGIN_INHERITED);
        CHECK(merged.spec.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].origin ==
              TIRED_ORIGIN_INHERITED);
        bool generic = strcmp(profile.id, "generic") == 0;
        CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART,
                                   generic ? "on-failure" : "always"));
        if (strcmp(profile.id, "backhaul") == 0)
        {
            CHECK(merged.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 3000000);
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].origin == TIRED_ORIGIN_INHERITED);
            CHECK(merged.decisions[2] == TIRED_RECOMMENDATION_CONDITION_UNKNOWN);
            context.nofile_known = true;
            context.nofile_ceiling.value = 4096;
            CHECK(tired_profile_merge(&profile, &spec, &context, &merged, &error));
            CHECK(merged.decisions[2] == TIRED_RECOMMENDATION_CONDITION_FALSE);
            context.nofile_ceiling.value = 1048576;
            CHECK(tired_profile_merge(&profile, &spec, &context, &merged, &error));
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].value.limit.value == 1048576);
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].value.limit.value == 1048576);
        }
        CHECK(tired_spec_set(&spec, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true, &error));
        CHECK(tired_profile_merge(&profile, &spec, &context, &merged, &error));
        CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_NETWORK, "none"));
        CHECK(tired_spec_set(&spec, TIRED_FIELD_RESTART, "no", 2, TIRED_ORIGIN_USER, true, &error));
        CHECK(tired_profile_merge(&profile, &spec, &context, &merged, &error));
        CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "no"));
        tired_text_destroy(&file);
        tired_text_destroy(&executable);
        tired_profile_destroy(&profile);
        tired_spec_destroy(&spec);
        tired_profile_merge_destroy(&merged);
    }
    return 0;
}
