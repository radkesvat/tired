#include "tired/io.h"
#include "tired/profile.h"
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
    TiredServiceSpec model = {0};
    TiredProfileMerge merged = {0};
    TiredProfileContext context = {0};
    CHECK(tired_spec_defaults(&model, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_VERSION_UNKNOWN);
    CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "on-failure"));
    context.systemd_version = 249;
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_APPLIED);
    CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "always"));
    CHECK(tired_spec_set(&model, TIRED_FIELD_RESTART, "no", 2, TIRED_ORIGIN_USER, false, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "no"));
    CHECK(tired_spec_inherit(&model, TIRED_FIELD_RESTART, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(merged.spec.fields[TIRED_FIELD_RESTART].origin == TIRED_ORIGIN_USER);
    CHECK(merged.spec.fields[TIRED_FIELD_RESTART].inherit);
    CHECK(!tired_field_has_value(&merged.spec.fields[TIRED_FIELD_RESTART]));
    CHECK(tired_spec_defaults(&model, &error));
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
    struct json_object *condition = json_object_new_object();
    CHECK(condition != NULL);
    CHECK(json_object_object_add(condition, "feature", json_object_new_string("memory-max")) == 0);
    CHECK(json_object_object_add(rec, "conditions", condition) == 0);
    const char *with_condition = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(with_condition, strlen(with_condition), &profile, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_CONDITION_UNKNOWN);
    context.features[1] = TIRED_FACT_FALSE;
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_CONDITION_FALSE);
    context.features[1] = TIRED_FACT_TRUE;
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_APPLIED);
    CHECK(json_object_object_add(rec, "field", json_object_new_string("timeout_start")) == 0);
    CHECK(json_object_object_add(rec, "strength",
                                 json_object_new_string("required-under-stated-conditions")) == 0);
    CHECK(json_object_object_add(rec, "value", json_object_new_string("infinity")) == 0);
    with_condition = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(with_condition, strlen(with_condition), &profile, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.spec.fields[TIRED_FIELD_TIMEOUT_START].value.timeout.infinity);
    CHECK(tired_spec_set(&model, TIRED_FIELD_TIMEOUT_START, "infinity", 8, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(tired_spec_set(&model, TIRED_FIELD_TIMEOUT_START, "10s", 3, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_REQUIRED_CONFLICT);
    CHECK(json_object_object_add(rec, "value", json_object_new_string("always")) == 0);
    CHECK(json_object_object_add(rec, "field", json_object_new_string("restart")) == 0);
    CHECK(json_object_object_add(rec, "strength", json_object_new_string("recommended")) == 0);
    json_object_object_del(rec, "conditions");
    struct json_object *other = NULL;
    const char *rec_text = json_object_to_json_string_ext(rec, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_json_parse(rec_text, strlen(rec_text), TIRED_PROFILE_LIMIT, &other, &error));
    CHECK(json_object_object_add(other, "value", json_object_new_string("no")) == 0);
    CHECK(json_object_array_add(recs, other) == 0);
    with_condition = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(with_condition, strlen(with_condition), &profile, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_CONFLICT &&
          merged.decisions[1] == TIRED_RECOMMENDATION_CONFLICT);
    CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "on-failure"));
    CHECK(json_object_object_add(other, "field", json_object_new_string("type")) == 0);
    CHECK(json_object_object_add(other, "value", json_object_new_string("simple")) == 0);
    condition = json_object_new_object();
    CHECK(condition != NULL);
    CHECK(json_object_object_add(condition, "type", json_object_new_string("simple")) == 0);
    CHECK(json_object_object_add(rec, "conditions", condition) == 0);
    with_condition = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(with_condition, strlen(with_condition), &profile, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_TYPE, "simple"));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_APPLIED);
    CHECK(tired_spec_set(&model, TIRED_FIELD_TYPE, "exec", 4, TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_CONDITION_FALSE);
    CHECK(merged.decisions[1] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(json_object_object_add(condition, "type", json_object_new_string("exec")) == 0);
    CHECK(json_object_object_add(rec, "strength",
                                 json_object_new_string("required-under-stated-conditions")) == 0);
    CHECK(tired_spec_set(&model, TIRED_FIELD_RESTART, "no", 2, TIRED_ORIGIN_USER, true, &error));
    with_condition = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(with_condition, strlen(with_condition), &profile, &error));
    CHECK(tired_profile_merge(&profile, &model, &context, &merged, &error));
    CHECK(merged.decisions[0] == TIRED_RECOMMENDATION_REQUIRED_CONFLICT);
    CHECK(tired_spec_choice_is(&merged.spec, TIRED_FIELD_RESTART, "no"));
    json_object_object_del(rec, "conditions");
    CHECK(json_object_object_add(rec, "strength", json_object_new_string("recommended")) == 0);
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
    tired_profile_merge_destroy(&merged);
    tired_spec_destroy(&model);
    return 0;
}
