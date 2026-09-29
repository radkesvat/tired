#ifndef TIRED_JOURNAL_OUTPUT_H
#define TIRED_JOURNAL_OUTPUT_H
#include "tired/journal_record.h"
#include <sys/types.h>
/* Render one decoded record as one newline-terminated text or NDJSON event.
 * Unit is the full selected .service name; UID identifies the selected user
 * manager, not the emitting process. System scope omits selected UID. JSON byte
 * fields use explicit hex encoding; text uses printable ASCII and byte escapes.
 * No implicit secret redaction: application logs may contain sensitive data.
 * Atomic owned output; never writes to a terminal, file, or network itself. */
bool tired_journal_output(const TiredJournalRecord *record, const char *unit, bool user_scope,
                          uid_t uid, bool json, TiredText *output, TiredError *error);
#endif
