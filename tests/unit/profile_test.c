#include "tired/io.h"
#include "tired/profile.h"
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
static const char fixture[] =
    "{\"schema_version\":1,\"id\":\"fixture\",\"name\":\"Fixture\",\"revision\":1,\"summary\":"
    "\"Parser fixture\","
    "\"match\":{\"executable_basenames\":[\"fixture\"],\"case_sensitive\":true},"
    "\"compatibility\":{\"systemd_min\":249,\"application_version\":null,\"version_policy\":"
    "\"version-independent-advice\"},"
    "\"recommendations\":[{\"field\":\"restart\",\"value\":\"always\",\"scope\":[\"system\","
    "\"user\"],\"apply\":\"automatic\","
    "\"strength\":\"recommended\",\"risk\":\"normal\",\"reason\":\"Test "
    "recommendation\",\"source_ids\":[\"evidence\"]}],"
    "\"advisories\":[],\"sources\":[{\"id\":\"evidence\",\"url\":\"https://example.org/"
    "fixture\",\"checked_at\":\"2026-09-28\",\"source_kind\":\"tested-recommendation\"}]}";
int main(int argc, char **argv)
{
    TiredProfile profile = {0};
    TiredError error = {0};
    CHECK(tired_profile_parse(fixture, strlen(fixture), &profile, &error));
    CHECK(profile.count == 1 && profile.recommendations[0].field == TIRED_FIELD_RESTART);
    CHECK(profile.recommendations[0].automatic && profile.recommendations[0].scopes == 3);
    TiredText executable = {0};
    CHECK(tired_text_set(&executable, "/tmp/fixture", 12, 128, &error));
    CHECK(tired_profile_matches(&profile, &executable));
    CHECK(tired_text_set(&executable, "/tmp/Fixture", 12, 128, &error));
    CHECK(!tired_profile_matches(&profile, &executable));
    const char *bad[] = {"null",
                         "1",
                         "[]",
                         "{}",
                         "{\"schema_version\":2}",
                         ("{\"schema_version\":1,\"revision\":1,\"id\":\"x\",\"name\":\"X\","
                          "\"summary\":\"X\",\"match\":1}")};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!tired_profile_parse(bad[i], strlen(bad[i]), &profile, &error));
    CHECK(strcmp(profile.id, "fixture") == 0);
    struct json_object *document = NULL;
    CHECK(tired_json_parse(fixture, strlen(fixture), TIRED_PROFILE_LIMIT, &document, &error));
    CHECK(json_object_object_add(document, "unknown_operation", json_object_new_boolean(true)) ==
          0);
    const char *unknown = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(unknown, strlen(unknown), &profile, &error));
    json_object_object_del(document, "unknown_operation");
    struct json_object *recs = NULL;
    CHECK(json_object_object_get_ex(document, "recommendations", &recs));
    struct json_object *rec = json_object_array_get_idx(recs, 0);
    CHECK(json_object_object_add(rec, "field", json_object_new_string("run_as")) == 0);
    const char *changed = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(changed, strlen(changed), &profile, &error));
    CHECK(json_object_object_add(rec, "field", json_object_new_string("restart")) == 0);
    CHECK(json_object_object_add(rec, "value", json_object_new_int(1)) == 0);
    changed = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(changed, strlen(changed), &profile, &error));
    CHECK(json_object_object_add(rec, "value", json_object_new_string("always")) == 0);
    CHECK(json_object_object_add(rec, "hook", json_object_new_string("evil")) == 0);
    changed = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(changed, strlen(changed), &profile, &error));
    json_object_object_del(rec, "hook");
    struct json_object *ids = json_object_new_array();
    CHECK(ids != NULL);
    CHECK(json_object_array_add(ids, json_object_new_string("missing")) == 0);
    CHECK(json_object_object_add(rec, "source_ids", ids) == 0);
    changed = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(changed, strlen(changed), &profile, &error));
    json_object_put(document);
    if (argc == 2)
    {
        TiredText schema = {0};
        document = NULL;
        CHECK(tired_read_file(argv[1], TIRED_INPUT_LIMIT, &schema, &error));
        CHECK(tired_json_parse(schema.data, schema.length, TIRED_INPUT_LIMIT, &document, &error));
        CHECK(json_object_is_type(document, json_type_object));
        json_object_put(document);
        tired_text_destroy(&schema);
    }
    tired_text_destroy(&executable);
    tired_profile_destroy(&profile);
    tired_profile_destroy(&profile);
    return 0;
}
