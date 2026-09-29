#ifndef TIRED_RISK_H
#define TIRED_RISK_H
#include "tired/model.h"
#include <sys/types.h>

typedef enum
{
    TIRED_RISK_ROOT,
    TIRED_RISK_CAPABILITIES,
    TIRED_RISK_WRITABLE_CODE,
    TIRED_RISK_RAPID_RETRY,
    TIRED_RISK_SENSITIVE_COMMAND,
    TIRED_RISK_RESTORE_DRIFT,
    TIRED_RISK_SENSITIVE_EXPORT,
    TIRED_RISK_COUNT
} TiredRiskId;
typedef struct
{
    const char *code, *message;
} TiredRisk;
typedef struct
{
    uid_t invoking_uid, service_uid;
    bool privileged_code_checked, privileged_code_writable;
    bool sensitive_command, restoring_drift, sensitive_export;
} TiredRiskFacts;
typedef struct
{
    bool present[TIRED_RISK_COUNT];
    bool pending[TIRED_RISK_COUNT];
} TiredRiskReport;
const TiredRisk *tired_risk_get(TiredRiskId id);
bool tired_risk_find(const char *code, size_t length, TiredRiskId *id);
/* Pure assessment of a validated model and supplied facts. Unknown filesystem
 * trust remains pending for privileged execution. Performs no I/O or approval. */
void tired_risk_assess(const TiredServiceSpec *spec, const TiredRiskFacts *facts,
                       TiredRiskReport *report);
/* Checks only this risk inventory; never authorizes filesystem/manager actions or
 * bypasses other validation. Pending checks cannot be acknowledged away. */
bool tired_risk_check_acknowledgments(const TiredRiskReport *report,
                                      const TiredTextList *acknowledgments, TiredError *error);
#endif
