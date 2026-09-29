#ifndef TIRED_LIST_FRONTEND_H
#define TIRED_LIST_FRONTEND_H
#include "tired/cli.h"
typedef enum
{
    TIRED_LIST_MATCH,
    TIRED_LIST_NO_MATCH,
    TIRED_LIST_UNKNOWN
} TiredListMatch;
/* AND of exact state/profile filters and ASCII-case-insensitive name substring.
 * NULL facts are unknown. A known mismatch wins; otherwise any required unknown
 * fact yields UNKNOWN, which listing retains with a diagnostic marker. */
TiredListMatch tired_list_match(const TiredRequest *request, const char *name, const char *active,
                                const char *enabled, const char *profile);
/* Read-only selected-scope listing. Includes per-record diagnostics and transaction
 * reservations without a current record. Fresh native manager batch; no activation.
 * Owned output and exit status are atomic. Partial results are explicit/nonzero. */
bool tired_list_command(const TiredRequest *request, TiredText *output, TiredStatus *result,
                        TiredError *error);
#endif
