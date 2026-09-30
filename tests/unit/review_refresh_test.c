#include "tired/ui.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s [%s]\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static const char profile[] =
    "{\"schema_version\":1,\"id\":\"conditional\",\"name\":\"Conditional\",\"revision\":1,"
    "\"summary\":\"Conditional review fixture\",\"match\":{\"executable_basenames\":[\"fixture\"],"
    "\"case_sensitive\":true},\"compatibility\":{\"systemd_min\":249,\"application_version\":null,"
    "\"version_policy\":\"version-independent-advice\"},\"recommendations\":["
    "{\"field\":\"restart_sec\",\"value\":\"9s\",\"scope\":[\"system\",\"user\"],\"apply\":"
    "\"automatic\","
    "\"strength\":\"recommended\",\"risk\":\"normal\",\"reason\":\"Exec delay\","
    "\"source_ids\":[\"test\"],\"conditions\":{\"type\":\"exec\"}},"
    "{\"field\":\"tasks_max\",\"value\":128,\"scope\":[\"system\",\"user\"],\"apply\":"
    "\"automatic\","
    "\"strength\":\"recommended\",\"risk\":\"resource-change\",\"reason\":\"Argument capacity\","
    "\"source_ids\":[\"test\"],\"conditions\":{\"argument\":\"--fast\"}}],\"advisories\":[],"
    "\"sources\":[{\"id\":\"test\",\"url\":\"https://example.org/"
    "fixture\",\"checked_at\":\"2026-09-30\","
    "\"source_kind\":\"tested-recommendation\"}]}";
int main(void)
{
    int result = 1;
    TiredError error = {0};
    TiredMutation mutation = {0};
    TiredBackend backend = {.features.systemd_version = 249};
    TiredSettings settings = {0};
    TiredServiceSpec base = {0};
    TiredProfileMerge headless = {0};
    TiredText evidence = {0};
    tired_settings_defaults(&settings);
    settings.restart_usec = 11000000;
    settings.supplied[TIRED_SETTING_RESTART_DELAY] = true;
    settings.origins[TIRED_SETTING_RESTART_DELAY] = TIRED_SETTINGS_ADMIN;
    CHECK(tired_spec_defaults(&mutation.proposed.spec, &error));
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_NAME, "fixture", 7,
                         TIRED_ORIGIN_CAPTURE, true, &error));
    CHECK(tired_plan_configured_defaults(&mutation.proposed.spec, &settings, &error));
    CHECK(tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "fixture", 7,
                            TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "--fast", 6,
                            TIRED_ORIGIN_CAPTURE, &error));
    CHECK(
        tired_profile_parse(profile, strlen(profile), &mutation.proposed.profile.profile, &error));
    mutation.proposed.has_profile = true;
    CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 9000000);
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_TASKS_MAX].value.limit.value == 128);
    CHECK(mutation.proposed.profile.decisions[0] == TIRED_RECOMMENDATION_APPLIED);
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_TYPE, "simple", 6, TIRED_ORIGIN_USER,
                         true, &error));
    mutation.proposed.review.acknowledged[TIRED_RISK_RAPID_RETRY] = true;
    CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 11000000);
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].origin ==
          TIRED_ORIGIN_CONFIG_ADMIN);
    CHECK(mutation.proposed.profile.decisions[0] == TIRED_RECOMMENDATION_CONDITION_FALSE);
    CHECK(!mutation.proposed.review.acknowledged[TIRED_RISK_RAPID_RETRY]);
    CHECK(tired_spec_defaults(&base, &error));
    CHECK(tired_plan_configured_defaults(&base, &settings, &error));
    CHECK(tired_spec_set(&base, TIRED_FIELD_TYPE, "simple", 6, TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_profile_merge(&mutation.proposed.profile.profile, &base, &backend.features,
                              &headless, &error));
    CHECK(headless.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds ==
          mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds);
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_TYPE, "exec", 4, TIRED_ORIGIN_USER,
                         true, &error));
    CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 9000000);
    CHECK(tired_spec_clear_list(&mutation.proposed.spec, TIRED_FIELD_ARGV, TIRED_ORIGIN_USER,
                                &error));
    CHECK(tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "fixture", 7,
                            TIRED_ORIGIN_USER, &error));
    CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
    CHECK(!tired_field_has_value(&mutation.proposed.spec.fields[TIRED_FIELD_TASKS_MAX]));
    CHECK(mutation.proposed.profile.decisions[1] == TIRED_RECOMMENDATION_CONDITION_FALSE);
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_RESTART_SEC, "7s", 2,
                         TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_spec_append(&mutation.proposed.spec, TIRED_FIELD_ARGV, "--fast", 6,
                            TIRED_ORIGIN_USER, &error));
    CHECK(tired_spec_inherit(&mutation.proposed.spec, TIRED_FIELD_TASKS_MAX, &error));
    CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 7000000);
    CHECK(mutation.proposed.profile.decisions[0] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(mutation.proposed.spec.fields[TIRED_FIELD_TASKS_MAX].inherit &&
          mutation.proposed.spec.fields[TIRED_FIELD_TASKS_MAX].origin == TIRED_ORIGIN_USER);
    CHECK(mutation.proposed.profile.decisions[1] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(strcmp(mutation.proposed.profile.profile.id, "conditional") == 0 &&
          mutation.proposed.profile.profile.revision == 1);
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_DESCRIPTION, "Profile description",
                         19, TIRED_ORIGIN_PROFILE, true, &error));
    CHECK(tired_spec_set(&mutation.proposed.spec, TIRED_FIELD_SYSLOG_IDENTIFIER, "profile-log", 11,
                         TIRED_ORIGIN_PROFILE, true, &error));
    CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
    CHECK(strcmp(mutation.proposed.spec.fields[TIRED_FIELD_DESCRIPTION].value.text.data,
                 "fixture (managed by tired)") == 0);
    CHECK(strcmp(mutation.proposed.spec.fields[TIRED_FIELD_SYSLOG_IDENTIFIER].value.text.data,
                 "fixture") == 0);
    CHECK(tired_ui_field_evidence(&mutation, TIRED_FIELD_RESTART_SEC, &evidence, &error));
    CHECK(strstr(evidence.data, "explicit user selection") != NULL &&
          strstr(evidence.data, "Exec delay") != NULL &&
          strstr(evidence.data, "user-override") != NULL &&
          strstr(evidence.data, "https://example.org/fixture") != NULL &&
          strstr(evidence.data, "tested-recommendation") != NULL &&
          strstr(evidence.data, "\"type\":\"exec\"") != NULL);
    CHECK(tired_ui_field_evidence(&mutation, TIRED_FIELD_TASKS_MAX, &evidence, &error));
    CHECK(strstr(evidence.data, "Explicit user inheritance") != NULL &&
          strstr(evidence.data, "Argument capacity") != NULL);
    CHECK(!tired_spec_uses_hardening_baseline(&mutation.proposed.spec));
    CHECK(tired_spec_hardening_baseline(&mutation.proposed.spec, &error));
    CHECK(tired_spec_uses_hardening_baseline(&mutation.proposed.spec) &&
          mutation.proposed.spec.fields[TIRED_FIELD_PRIVATE_TMP].origin == TIRED_ORIGIN_USER);
    CHECK(tired_spec_choice_is(&mutation.proposed.spec, TIRED_FIELD_TYPE, "exec") &&
          mutation.proposed.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 7000000);
    result = 0;
cleanup:
    tired_mutation_destroy(&mutation);
    tired_spec_destroy(&base);
    tired_profile_merge_destroy(&headless);
    tired_settings_destroy(&settings);
    tired_text_destroy(&evidence);
    return result;
}
