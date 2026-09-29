#ifndef TIRED_LIST_FRONTEND_H
#define TIRED_LIST_FRONTEND_H
#include "tired/cli.h"
/* Read-only selected-scope listing. Includes per-record diagnostics and transaction
 * reservations without a current record. Fresh native manager batch; no activation.
 * Owned output and exit status are atomic. Partial results are explicit/nonzero. */
bool tired_list_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                        TiredError *error);
#endif
