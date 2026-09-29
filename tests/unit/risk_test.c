#include "tired/risk.h"
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
    TiredServiceSpec spec = {0};
    TiredRiskReport report = {0};
    TiredRiskFacts facts = {.invoking_uid = 1000, .service_uid = 1000};
    TiredTextList accepted = {0};
    TiredError error = {0};
    CHECK(tired_spec_defaults(&spec, &error));
    tired_risk_assess(&spec, &facts, &report);
    CHECK(tired_risk_check_acknowledgments(&report, &accepted, &error));
    facts.service_uid = 0;
    tired_risk_assess(&spec, &facts, &report);
    CHECK(report.present[TIRED_RISK_ROOT] && report.pending[TIRED_RISK_WRITABLE_CODE]);
    for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
    {
        const TiredRisk *risk = tired_risk_get((TiredRiskId)i);
        TiredRiskId id;
        CHECK(tired_risk_find(risk->code, strlen(risk->code), &id) && (unsigned)id == i);
        CHECK(tired_text_list_append(&accepted, risk->code, strlen(risk->code), 32, 4096, &error));
    }
    CHECK(!tired_risk_check_acknowledgments(&report, &accepted, &error));
    CHECK(strcmp(error.code, "risk-check-pending") == 0);
    facts.privileged_code_checked = true;
    facts.privileged_code_writable = true;
    tired_risk_assess(&spec, &facts, &report);
    CHECK(report.present[TIRED_RISK_WRITABLE_CODE] && !report.pending[TIRED_RISK_WRITABLE_CODE]);
    CHECK(tired_risk_check_acknowledgments(&report, &accepted, &error));
    tired_text_list_destroy(&accepted);
    CHECK(!tired_risk_check_acknowledgments(&report, &accepted, &error));
    CHECK(strcmp(error.code, "run-as-root") == 0);
    facts.invoking_uid = 0;
    facts.privileged_code_writable = false;
    tired_risk_assess(&spec, &facts, &report);
    CHECK(!report.present[TIRED_RISK_ROOT]);
    CHECK(tired_risk_check_acknowledgments(&report, &accepted, &error));
    CHECK(tired_spec_set(&spec, TIRED_FIELD_RESTART_SEC, "999ms", 5, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_append(&spec, TIRED_FIELD_AMBIENT_CAPABILITIES, "CAP_NET_BIND_SERVICE", 20,
                            TIRED_ORIGIN_USER, &error));
    facts = (TiredRiskFacts){.invoking_uid = 1000,
                             .service_uid = 1000,
                             .sensitive_command = true,
                             .restoring_drift = true,
                             .sensitive_export = true};
    tired_risk_assess(&spec, &facts, &report);
    CHECK(report.present[TIRED_RISK_CAPABILITIES]);
    CHECK(report.pending[TIRED_RISK_WRITABLE_CODE]);
    CHECK(report.present[TIRED_RISK_RAPID_RETRY]);
    CHECK(report.present[TIRED_RISK_SENSITIVE_COMMAND] &&
          report.present[TIRED_RISK_RESTORE_DRIFT] && report.present[TIRED_RISK_SENSITIVE_EXPORT]);
    CHECK(tired_spec_set(&spec, TIRED_FIELD_RESTART_SEC, "1s", 2, TIRED_ORIGIN_USER, true, &error));
    tired_risk_assess(&spec, &facts, &report);
    CHECK(!report.present[TIRED_RISK_RAPID_RETRY]);
    CHECK(tired_spec_set(&spec, TIRED_FIELD_RETRY_POLICY, "limited", 7, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_set(&spec, TIRED_FIELD_RESTART_SEC, "0", 1, TIRED_ORIGIN_USER, true, &error));
    tired_risk_assess(&spec, &facts, &report);
    CHECK(!report.present[TIRED_RISK_RAPID_RETRY]);
    CHECK(tired_text_list_append(&accepted, "unknown", 7, 32, 4096, &error));
    CHECK(!tired_risk_check_acknowledgments(&report, &accepted, &error));
    CHECK(strcmp(error.code, "unknown-risk") == 0);
    tired_text_list_destroy(&accepted);
    tired_spec_destroy(&spec);
    return 0;
}
