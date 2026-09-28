#ifndef TIRED_PLAN_OUTPUT_H
#define TIRED_PLAN_OUTPUT_H
#include "tired/plan.h"
/* Read-only preview output. Sensitive command flags use heuristic classification;
 * unknown secret forms can escape classification. include_sensitive requires
 * private export authorization at the frontend boundary. */
bool tired_plan_output(const TiredPlan *plan, bool json, bool unit_only, bool include_sensitive,
                       TiredText *output, TiredError *error);
#endif
