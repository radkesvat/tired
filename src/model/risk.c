#include "tired/risk.h"
#include <assert.h>
#include <string.h>

static const TiredRisk risks[TIRED_RISK_COUNT] = {
    {"run-as-root", "This proposal selects root execution from a nonroot invocation."},
    {"privileged-capabilities", "This proposal grants ambient capabilities to the workload."},
    {"writable-privileged-code",
     "Privileged execution uses code or parent directories writable by less-trusted identities."},
    {"rapid-persistent-retry", "Persistent retries use a delay below one second."},
    {"sensitive-command-data",
     "Sensitive command data remains in the command and generated unit metadata."},
    {"restore-drifted-unit", "This operation replaces foreign edits with managed configuration."},
    {"sensitive-export", "This export writes unredacted data to a new private file."}};
const TiredRisk *tired_risk_get(TiredRiskId id)
{
    return (unsigned)id < TIRED_RISK_COUNT ? &risks[id] : NULL;
}
bool tired_risk_find(const char *code, size_t length, TiredRiskId *id)
{
    assert(code != NULL && id != NULL);
    for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
        if (strlen(risks[i].code) == length && memcmp(code, risks[i].code, length) == 0)
        {
            *id = (TiredRiskId)i;
            return true;
        }
    return false;
}
void tired_risk_assess(const TiredServiceSpec *spec, const TiredRiskFacts *facts,
                       TiredRiskReport *report)
{
    assert(spec != NULL && facts != NULL && report != NULL);
    *report = (TiredRiskReport){0};
    report->present[TIRED_RISK_ROOT] = facts->invoking_uid != 0 && facts->service_uid == 0;
    const TiredFieldValue *caps = &spec->fields[TIRED_FIELD_AMBIENT_CAPABILITIES];
    bool grants = tired_field_has_value(caps) && caps->value.list.count != 0;
    report->present[TIRED_RISK_CAPABILITIES] = grants;
    if (facts->service_uid == 0 || grants)
    {
        report->pending[TIRED_RISK_WRITABLE_CODE] = !facts->privileged_code_checked;
        report->present[TIRED_RISK_WRITABLE_CODE] =
            facts->privileged_code_checked && facts->privileged_code_writable;
    }
    const TiredFieldValue *delay = &spec->fields[TIRED_FIELD_RESTART_SEC];
    report->present[TIRED_RISK_RAPID_RETRY] =
        tired_spec_choice_is(spec, TIRED_FIELD_RETRY_POLICY, "persistent") &&
        tired_field_has_value(delay) && delay->value.microseconds < 1000000;
    report->present[TIRED_RISK_SENSITIVE_COMMAND] = facts->sensitive_command;
    report->present[TIRED_RISK_RESTORE_DRIFT] = facts->restoring_drift;
    report->present[TIRED_RISK_SENSITIVE_EXPORT] = facts->sensitive_export;
}
bool tired_risk_check_acknowledgments(const TiredRiskReport *report,
                                      const TiredTextList *acknowledgments, TiredError *error)
{
    assert(report != NULL && acknowledgments != NULL);
    bool accepted[TIRED_RISK_COUNT] = {false};
    for (size_t i = 0; i < acknowledgments->count; ++i)
    {
        TiredRiskId id;
        const TiredText *code = &acknowledgments->items[i];
        if (!tired_risk_find(code->data, code->length, &id))
            return tired_error_set(error, TIRED_INVALID, "unknown-risk",
                                   "Risk acknowledgment names an unsupported code.", 0);
        accepted[id] = true;
    }
    for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
        if (report->pending[i])
            return tired_error_set(error, TIRED_INVALID, "risk-check-pending",
                                   "Required risk inspection has not completed.", 0);
    for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
        if (report->present[i] && !accepted[i])
            return tired_error_set(error, TIRED_INVALID, risks[i].code, risks[i].message, 0);
    tired_error_clear(error);
    return true;
}
