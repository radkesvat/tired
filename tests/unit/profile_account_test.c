#include "tired/frontend.h"
#include "tired/plan_output.h"
#include "tired/ui.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

int main(int argc, char **argv)
{
    int result = 1;
    TiredError error = {0};
    TiredProfileCatalog catalog = {0};
    TiredAccount ordinary = {0}, root = {0};
    TiredRequest request = {0};
    TiredPlan plan = {0};
    TiredText output = {0};
    TiredMutation mutation = {0};
    TiredProfile parsed = {0};
    struct json_object *document = NULL;
    TiredBackend backend = {.features.systemd_version = 249};
    TiredSettings settings = {0};
    tired_settings_defaults(&settings);
    const char *args[] = {"tired", "plan", "--offline", "--", "/bin/true"};
    CHECK(argc == 2);
    CHECK(tired_account_resolve("nobody", 6, &ordinary, &error) && ordinary.uid != 0);
    CHECK(tired_account_by_uid(0, &root, &error));
    CHECK(tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                      &error));
    CHECK(tired_cli_parse(5, args, &request, &error));
    for (unsigned scenario = 0; scenario < 8; ++scenario)
    {
        tired_plan_destroy(&plan);
        CHECK(tired_plan_prepare(&request, &plan, &error));
        /* Simulate a captured nonroot account without changing the test process identity. */
        CHECK(tired_account_by_uid(ordinary.uid, &plan.invoking, &error));
        CHECK(tired_account_by_uid(ordinary.uid, &plan.service, &error));
        CHECK(tired_group_by_gid(ordinary.primary_group.gid, &plan.group, &error));
        CHECK(
            tired_spec_set(&plan.spec, TIRED_FIELD_RUN_AS, ordinary.name.data, ordinary.name.length,
                           scenario == 1 ? TIRED_ORIGIN_USER : TIRED_ORIGIN_CAPTURE, true, &error));
        CHECK(tired_spec_set(&plan.spec, TIRED_FIELD_GROUP, ordinary.primary_group.name.data,
                             ordinary.primary_group.name.length,
                             scenario == 2 ? TIRED_ORIGIN_USER : TIRED_ORIGIN_CAPTURE, true,
                             &error));
        if (scenario == 3)
            CHECK(tired_spec_set(&plan.spec, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true,
                                 &error));
        if (scenario == 4)
            plan.account_defaults_allowed = false;
        backend.features.systemd_version = scenario == 6 ? 248 : scenario == 7 ? 0 : 249;
        CHECK(tired_text_set(&plan.invocation.executable, "/opt/backhaul", 13, 4096, &error));
        CHECK(tired_plan_apply_profiles(&plan, &catalog, scenario == 5 ? "none" : "auto",
                                        &backend.features, &error));
        bool defaults_to_root = scenario == 0 || scenario == 2;
        CHECK(plan.service.uid == (defaults_to_root ? 0 : ordinary.uid));
        CHECK(strcmp(plan.spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                     defaults_to_root ? root.name.data : ordinary.name.data) == 0);
        CHECK(plan.group.gid ==
              (scenario == 0 ? root.primary_group.gid : ordinary.primary_group.gid));
        CHECK(strcmp(plan.spec.fields[TIRED_FIELD_GROUP].value.text.data,
                     scenario == 0 ? root.primary_group.name.data
                                   : ordinary.primary_group.name.data) == 0);
        if (scenario == 0)
        {
            CHECK(tired_plan_output(&plan, true, false, false, &output, &error));
            CHECK(strstr(output.data, "run-as-root") != NULL);
            TiredRiskFacts facts = {.invoking_uid = plan.invoking.uid,
                                    .service_uid = plan.service.uid,
                                    .privileged_code_checked = true};
            TiredRiskReport risks = {0};
            TiredTextList acknowledgments = {0};
            tired_risk_assess(&plan.spec, &facts, &risks);
            CHECK(risks.present[TIRED_RISK_ROOT]);
            CHECK(!tired_risk_check_acknowledgments(&risks, &acknowledgments, &error));
            CHECK(tired_spec_encode(&plan.spec, &output, &error));
            CHECK(tired_spec_parse(output.data, output.length, &mutation.proposed.spec, &error));
            const char *json =
                json_object_to_json_string_ext(plan.profile.document, JSON_C_TO_STRING_PLAIN);
            CHECK(tired_profile_parse(json, strlen(json), &mutation.proposed.profile.profile,
                                      &error));
            mutation.proposed.has_profile = true;
            CHECK(tired_ui_review_recompute(&mutation, &backend, &settings, &error));
            CHECK(strcmp(mutation.proposed.spec.fields[TIRED_FIELD_RUN_AS].value.text.data,
                         root.name.data) == 0);
        }
        if (scenario == 4)
        {
            /* Refreshing a saved service must not reapply a creation-time account default. */
            CHECK(plan.profile.default_root && plan.service.uid == ordinary.uid);
            CHECK(tired_json_parse(
                json_object_to_json_string_ext(plan.profile.document, JSON_C_TO_STRING_PLAIN),
                strlen(
                    json_object_to_json_string_ext(plan.profile.document, JSON_C_TO_STRING_PLAIN)),
                TIRED_PROFILE_LIMIT, &document, &error));
        }
    }
    CHECK(document != NULL);
    const char *json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(json, strlen(json), &parsed, &error) && parsed.default_root);
    CHECK(json_object_object_add(document, "default_run_as", json_object_new_string("nobody")) ==
          0);
    json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(json, strlen(json), &parsed, &error));
    CHECK(parsed.default_root);
    CHECK(json_object_object_add(document, "default_run_as", json_object_new_boolean(true)) == 0);
    json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(json, strlen(json), &parsed, &error));
    CHECK(json_object_object_add(document, "default_run_as",
                                 json_object_new_string_len("root\0other", 10)) == 0);
    json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_parse(json, strlen(json), &parsed, &error));
    json_object_object_del(document, "default_run_as");
    json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_profile_parse(json, strlen(json), &parsed, &error) && !parsed.default_root);
    result = 0;
cleanup:
    json_object_put(document);
    tired_profile_destroy(&parsed);
    tired_mutation_destroy(&mutation);
    tired_settings_destroy(&settings);
    tired_text_destroy(&output);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    tired_catalog_destroy(&catalog);
    tired_account_destroy(&ordinary);
    tired_account_destroy(&root);
    return result;
}
