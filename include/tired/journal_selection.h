#ifndef TIRED_JOURNAL_SELECTION_H
#define TIRED_JOURNAL_SELECTION_H
#include "tired/value.h"
#include <sys/types.h>
#include <systemd/sd-journal.h>
typedef struct
{
    char field[32], value[256];
} TiredJournalTerm;
typedef struct
{
    TiredJournalTerm terms[7]; /* Distinct fields: AND. */
    size_t count;
} TiredJournalClause;
typedef struct
{
    TiredJournalClause clauses[3]; /* OR. */
    size_t count;
} TiredJournalSelection;
typedef struct
{
    const char *name;
    TiredText value; /* Borrowed length-delimited native field value, without KEY=. */
} TiredJournalField;
/* Full safe .service name; user UID must already be authorized by the caller.
 * Optional boot is exactly 32 lowercase hex digits, not a service UUID.
 * Selects application records via trusted _SYSTEMD_* fields and manager records
 * with additional trusted origin constraints, never UNIT/USER_UNIT alone.
 * Pure construction, atomic fixed-size output. Does not open/broaden journal access. */
bool tired_journal_selection_build(const char *unit, bool user_scope, uid_t uid, const char *boot,
                                   TiredJournalSelection *output, TiredError *error);
/* Match decoded native fields against a built selection. Missing/binary/mismatched
 * identity fields do not match. Payload UNIT fields alone never establish origin. */
bool tired_journal_selection_matches(const TiredJournalSelection *selection,
                                     const TiredJournalField *fields, size_t count);
/* Install a built selection on an exclusively owned, newly opened handle before
 * any iteration. Does not flush prior filters. On failure close/discard the
 * handle; never iterate partially configured matches. */
bool tired_journal_selection_apply(sd_journal *journal, const TiredJournalSelection *selection,
                                   TiredError *error);
#endif
