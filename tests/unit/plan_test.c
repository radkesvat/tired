#include "tired/json.h"
#include "tired/plan_output.h"
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
int main(int argc, char **argv)
{
    const char *args[] = {"tired",
                          "plan",
                          "--offline",
                          "--profile",
                          "none",
                          "--name",
                          "custom",
                          "--retry-policy",
                          "limited",
                          "--unset",
                          "restart",
                          "--working-directory",
                          ".",
                          "--env",
                          "API_TOKEN=environment-secret",
                          "--credential",
                          "token=private-source",
                          "--env-file",
                          "external-file",
                          "--",
                          "/proc/self/exe",
                          "--password",
                          "command-secret",
                          "--token=attached-secret",
                          "",
                          "\xc2\x9b"};
    TiredRequest request = {0};
    TiredPlan plan = {0};
    TiredError error = {0};
    TiredText output = {0};
    struct json_object *json = NULL;
    CHECK(tired_cli_parse((int)(sizeof(args) / sizeof(args[0])), args, &request, &error));
    CHECK(tired_plan_prepare(&request, &plan, &error));
    CHECK(strcmp(plan.spec.fields[TIRED_FIELD_NAME].value.text.data, "custom") == 0);
    CHECK(strcmp(plan.spec.fields[TIRED_FIELD_DESCRIPTION].value.text.data,
                 "custom (managed by tired)") == 0);
    CHECK(plan.spec.fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds == 300000000);
    CHECK(plan.managed_environment.data != NULL &&
          strstr(plan.managed_environment.data, plan.uuid) != NULL);
    CHECK(plan.credentials.count == 1 && plan.credentials.items[0].path.data[0] == '/');
    CHECK(tired_plan_output(&plan, true, false, false, &output, &error));
    CHECK(strstr(output.data, "environment-secret") == NULL &&
          strstr(output.data, "command-secret") == NULL &&
          strstr(output.data, "attached-secret") == NULL);
    CHECK(strstr(output.data, "\\u009b") != NULL);
    CHECK(strstr(output.data, "\xc2\x9b") == NULL);
    /* Output may exceed generic input limit in general; this fixture is small. */
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &json, &error));
    struct json_object *flag = NULL;
    CHECK(json_object_object_get_ex(json, "replayable", &flag) && !json_object_get_boolean(flag));
    CHECK(json_object_object_get_ex(json, "unit_redacted", &flag) && json_object_get_boolean(flag));
    CHECK(tired_plan_output(&plan, false, true, false, &output, &error));
    CHECK(strstr(output.data, "Redacted non-installable") != NULL &&
          strstr(output.data, "command-secret") == NULL);
    CHECK(tired_plan_output(&plan, true, false, true, &output, &error));
    CHECK(strstr(output.data, "environment-secret") != NULL &&
          strstr(output.data, "command-secret") != NULL);
    CHECK(strstr(plan.spec.fields[TIRED_FIELD_ARGV].value.list.items[2].data, "command-secret") !=
          NULL);
    CHECK(argc == 2);
    TiredProfileCatalog catalog = {0};
    TiredProfileContext context = {.systemd_version = 249};
    CHECK(tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                      &error));
    CHECK(tired_plan_apply_profiles(&plan, &catalog, "backhaul", &context, &error));
    tired_catalog_destroy(&catalog);
    CHECK(strcmp(plan.profile.id, "backhaul") == 0 && strlen(plan.profile_digest) == 64);
    CHECK(plan.profile_decisions[2] == TIRED_RECOMMENDATION_CONDITION_UNKNOWN);
    CHECK(plan.profile_decisions[0] == TIRED_RECOMMENDATION_USER_OVERRIDE);
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART].inherit);
    CHECK(tired_plan_output(&plan, true, false, false, &output, &error));
    CHECK(strstr(output.data, "profile_snapshot") != NULL &&
          strstr(output.data, "condition-unknown") != NULL);
    CHECK(strstr(output.data, "command-secret") == NULL);
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &json, &error));
    CHECK(json_object_object_get_ex(json, "profile", &flag) &&
          strcmp(json_object_get_string(flag), "backhaul") == 0);
    CHECK(tired_plan_output(&plan, false, false, false, &output, &error));
    CHECK(strstr(output.data, "Profile: Backhaul") != NULL &&
          strstr(output.data, "Evidence:") != NULL);
    CHECK(strstr(output.data, "\nRestart=") == NULL);
    json_object_put(json);
    TiredSettings settings = {0}, layer = {0};
    tired_settings_defaults(&settings);
    const char *configuration =
        "{\"schema_version\":1,\"retry_policy\":\"limited\",\"restart_sec\":\"7s\"}";
    CHECK(tired_settings_parse(configuration, strlen(configuration), &layer, &error));
    CHECK(tired_settings_merge(&settings, &layer, TIRED_SETTINGS_ADMIN, &error));
    const char *configured[] = {"tired", "plan", "--offline",    "--profile",
                                "none",  "--",   "/usr/bin/true"};
    CHECK(tired_cli_parse(7, configured, &request, &error));
    CHECK(tired_plan_prepare_settings(&request, &settings, &plan, &error));
    CHECK(tired_spec_choice_is(&plan.spec, TIRED_FIELD_RETRY_POLICY, "limited"));
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 7000000);
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].origin == TIRED_ORIGIN_CONFIG_ADMIN);
    CHECK(plan.spec.fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds == 300000000);
    CHECK(plan.spec.fields[TIRED_FIELD_START_LIMIT_BURST].value.integer == 10);
    CHECK(tired_plan_output(&plan, true, false, false, &output, &error));
    CHECK(strstr(output.data, "administrator-config") != NULL);
    CHECK(tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                      &error));
    CHECK(tired_plan_apply_profiles(&plan, &catalog, "backhaul", &context, &error));
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 3000000);
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].origin == TIRED_ORIGIN_PROFILE);
    const char *explicit_args[] = {"tired",        "plan",          "--offline", "--retry-policy",
                                   "persistent",   "--restart-sec", "9s",        "--",
                                   "/usr/bin/true"};
    CHECK(tired_cli_parse(9, explicit_args, &request, &error));
    CHECK(tired_plan_prepare_settings(&request, &settings, &plan, &error));
    CHECK(plan.spec.fields[TIRED_FIELD_START_LIMIT_INTERVAL].value.microseconds == 0);
    CHECK(tired_plan_apply_profiles(&plan, &catalog, "backhaul", &context, &error));
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 9000000);
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].origin == TIRED_ORIGIN_USER);
    CHECK(tired_settings_merge(&settings, &layer, TIRED_SETTINGS_USER, &error));
    CHECK(tired_cli_parse(7, configured, &request, &error));
    CHECK(tired_plan_prepare_settings(&request, &settings, &plan, &error));
    CHECK(plan.spec.fields[TIRED_FIELD_RESTART_SEC].origin == TIRED_ORIGIN_CONFIG_USER);
    tired_catalog_destroy(&catalog);
    tired_settings_destroy(&layer);
    tired_settings_destroy(&settings);
    tired_text_destroy(&output);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    return 0;
}
