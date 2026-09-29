#ifndef TIRED_RECOVER_FRONTEND_H
#define TIRED_RECOVER_FRONTEND_H
#include "tired/cli.h"
/* Read-only stored-journal inspection. No manager reconciliation or resolution
 * actions yet. Successful inspection returns a complete output and result 0 or 8
 * (incomplete/nonterminal transactions); failures preserve output/result. */
bool tired_recover_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                           TiredError *error);
#endif
