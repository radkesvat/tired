#include "tired/catalog.h"
#include "tired/io.h"
#include "tired/profile_merge.h"
#include "tired/render.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool conservative(const char *id)
{
    static const char *ids[] = {"naiveproxy", "socat", "iperf3", "autossh", "ssh"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        if (strcmp(id, ids[i]) == 0)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    static const char *ids[] = {
        "generic",    "backhaul", "xray",     "sing-box",       "waterwall",   "mihomo",
        "clash-meta", "v2ray",    "hysteria", "tuic",           "trojan-go",   "sslocal",
        "ssserver",   "gost",     "brook",    "naiveproxy",     "shadow-tls",  "realm",
        "rathole",    "frpc",     "frps",     "chisel",         "cloudflared", "socat",
        "caddy",      "traefik",  "coredns",  "dnscrypt-proxy", "mosdns",      "iperf3",
        "autossh",    "ssh",      "nginx"};
    TiredProfileCatalog catalog = {0};
    TiredError error = {0};
    CHECK(argc == 2);
    CHECK(tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                      &error));
    CHECK(catalog.count == sizeof(ids) / sizeof(ids[0]));
    for (size_t index = 0; index < sizeof(ids) / sizeof(ids[0]); ++index)
    {
        TiredText executable = {0};
        const TiredProfileEntry *selected = NULL;
        size_t matches = 0;
        CHECK(tired_text_set(&executable, ids[index], strlen(ids[index]), 128, &error));
        CHECK(tired_catalog_select(&catalog, &executable, ids[index], false, &selected, &matches,
                                   &error));
        CHECK(matches == 1 && selected != NULL && strcmp(selected->profile.id, ids[index]) == 0);
        const TiredProfile *profile = &selected->profile;
        for (size_t alias = 0; alias < profile->basenames.count; ++alias)
        {
            const TiredText *name = &profile->basenames.items[alias];
            char path[512];
            CHECK(snprintf(path, sizeof(path), "/opt/network/%s", name->data) > 0);
            CHECK(tired_text_set(&executable, path, strlen(path), sizeof(path), &error));
            CHECK(tired_catalog_select(&catalog, &executable, "auto", false, &selected, &matches,
                                       &error));
            CHECK(matches == 1 && selected != NULL &&
                  selected->profile.document == profile->document);
        }
        CHECK(tired_text_set(&executable, "frp", 3, 128, &error));
        CHECK(!tired_profile_matches(profile, &executable));
        CHECK(tired_text_set(&executable, "unknown-program", 15, 128, &error));
        CHECK(!tired_profile_matches(profile, &executable));
        TiredServiceSpec spec = {0};
        TiredProfileMerge merged = {0};
        TiredProfileContext context = {.systemd_version = 249};
        CHECK(tired_spec_defaults(&spec, &error));
        CHECK(tired_spec_set(&spec, TIRED_FIELD_NAME, "fixture", 7, TIRED_ORIGIN_CAPTURE, true,
                             &error));
        CHECK(tired_spec_set(&spec, TIRED_FIELD_EXECUTABLE, "/opt/network/fixture", 20,
                             TIRED_ORIGIN_CAPTURE, true, &error));
        CHECK(tired_spec_set(&spec, TIRED_FIELD_WORKING_DIRECTORY, "/opt/network", 12,
                             TIRED_ORIGIN_CAPTURE, true, &error));
        const char *args[] = {profile->id, "-c", "relative config.toml"};
        for (size_t i = 0; i < 3; ++i)
            CHECK(tired_spec_append(&spec, TIRED_FIELD_ARGV, args[i], strlen(args[i]),
                                    TIRED_ORIGIN_CAPTURE, &error));
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        CHECK(merged.spec.fields[TIRED_FIELD_ARGV].value.list.count == 3);
        CHECK(strcmp(merged.spec.fields[TIRED_FIELD_ARGV].value.list.items[2].data, args[2]) == 0);
        CHECK(strcmp(merged.spec.fields[TIRED_FIELD_WORKING_DIRECTORY].value.text.data,
                     "/opt/network") == 0);
        CHECK(merged.spec.fields[TIRED_FIELD_RUN_AS].origin == TIRED_ORIGIN_INHERITED);
        CHECK(merged.spec.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].origin ==
              TIRED_ORIGIN_INHERITED);
        CHECK(merged.spec.fields[TIRED_FIELD_CAPABILITY_BOUNDING_SET].origin ==
              TIRED_ORIGIN_INHERITED);
        bool generic = strcmp(profile->id, "generic") == 0;
        bool high = !generic && !conservative(profile->id);
        bool continuous = strcmp(profile->id, "backhaul") == 0 ||
                          strcmp(profile->id, "frpc") == 0 || strcmp(profile->id, "frps") == 0;
        CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART,
                                   continuous ? "always" : "on-failure"));
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].origin == TIRED_ORIGIN_INHERITED);
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].origin == TIRED_ORIGIN_INHERITED);
        for (size_t i = 0; i < profile->count; ++i)
            if (profile->recommendations[i].field == TIRED_FIELD_NOFILE_SOFT ||
                profile->recommendations[i].field == TIRED_FIELD_NOFILE_HARD)
                CHECK(merged.decisions[i] == TIRED_RECOMMENDATION_CONDITION_UNKNOWN);
        context.nofile_known = true;
        context.nofile_ceiling.value = 4096;
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].origin == TIRED_ORIGIN_INHERITED);
        for (size_t i = 0; i < profile->count; ++i)
            if (profile->recommendations[i].field == TIRED_FIELD_NOFILE_SOFT ||
                profile->recommendations[i].field == TIRED_FIELD_NOFILE_HARD)
                CHECK(merged.decisions[i] == TIRED_RECOMMENDATION_CONDITION_FALSE);
        context.nofile_ceiling.value = 1048576;
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        if (high)
        {
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].value.limit.value == 1048576);
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].value.limit.value == 1048576);
            TiredText unit = {0};
            CHECK(tired_spec_set(&merged.spec, TIRED_FIELD_RUN_AS, "nobody", 6,
                                 TIRED_ORIGIN_CAPTURE, true, &error));
            CHECK(tired_spec_set(&merged.spec, TIRED_FIELD_GROUP, "nogroup", 7,
                                 TIRED_ORIGIN_CAPTURE, true, &error));
            CHECK(tired_spec_resolve_scope(&merged.spec, &error));
            CHECK(tired_render_unit(&merged.spec, "6d48db39-65c0-4de0-ae2a-c5e0208ea1d7", NULL,
                                    NULL, &unit, &error));
            CHECK(strstr(unit.data, "LimitNOFILE=1048576:1048576") != NULL);
            tired_text_destroy(&unit);
        }
        else
        {
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].origin == TIRED_ORIGIN_INHERITED);
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].origin == TIRED_ORIGIN_INHERITED);
            for (size_t i = 0; i < profile->count; ++i)
                CHECK(merged.decisions[i] == TIRED_RECOMMENDATION_SUGGESTION);
        }
        if (strcmp(profile->id, "backhaul") == 0)
            CHECK(merged.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 3000000);
        if (strcmp(profile->id, "nginx") == 0)
            CHECK(merged.spec.fields[TIRED_FIELD_KILL_SIGNAL].value.signal_number == SIGQUIT);
        CHECK(tired_spec_set(&spec, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true, &error));
        context.nofile_ceiling.value = 524288;
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].origin == TIRED_ORIGIN_INHERITED);
        CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_NETWORK, "none"));
        context.nofile_ceiling.value = 1048576;
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        if (high && strcmp(profile->id, "waterwall") != 0)
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].value.limit.value == 1048576);
        if (strcmp(profile->id, "waterwall") == 0)
        {
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].origin == TIRED_ORIGIN_INHERITED);
            CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].origin == TIRED_ORIGIN_INHERITED);
            for (size_t i = 0; i < profile->count; ++i)
                CHECK(merged.decisions[i] == TIRED_RECOMMENDATION_SCOPE);
        }
        CHECK(tired_spec_set(&spec, TIRED_FIELD_RESTART, "no", 2, TIRED_ORIGIN_USER, true, &error));
        CHECK(tired_spec_set(&spec, TIRED_FIELD_NOFILE_SOFT, "2048", 4, TIRED_ORIGIN_USER, true,
                             &error));
        CHECK(tired_spec_set(&spec, TIRED_FIELD_NOFILE_HARD, "4096", 4, TIRED_ORIGIN_USER, true,
                             &error));
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "no"));
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].value.limit.value == 2048);
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].value.limit.value == 4096);
        CHECK(tired_spec_inherit(&spec, TIRED_FIELD_NOFILE_SOFT, &error));
        CHECK(tired_spec_inherit(&spec, TIRED_FIELD_NOFILE_HARD, &error));
        CHECK(tired_profile_merge(profile, &spec, &context, &merged, &error));
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_SOFT].inherit);
        CHECK(merged.spec.fields[TIRED_FIELD_NOFILE_HARD].inherit);
        tired_text_destroy(&executable);
        tired_spec_destroy(&spec);
        tired_profile_merge_destroy(&merged);
    }
    tired_catalog_destroy(&catalog);
    return 0;
}
