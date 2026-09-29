#ifndef TIRED_JOURNAL_ACCESS_H
#define TIRED_JOURNAL_ACCESS_H
#include "tired/value.h"
#include <sys/types.h>
#define TIRED_JOURNAL_SCAN_LIMIT 4096U
typedef struct
{
    size_t entries_examined, directories_opened, files_seen, files_opened, missing_roots, issues;
    bool complete; /* Enumeration and native file opening only, not content integrity. */
    TiredError first_issue;
} TiredJournalAccess;
/* Read-only default-namespace inventory matching LOCAL_ONLY selection. Runtime
 * root admits all machine-ID directories; persistent root admits local machine
 * only. User scope checks system and selected user files; system scope checks all
 * file types, including service messages potentially stored in user journals.
 * UID must match the intended native open flags and already be authorized.
 * roots is NULL for /run/log/journal and /var/log/journal, or two trusted test
 * paths in that order. Does not follow final symlinks, elevate, or iterate entries.
 * Missing roots are reported separately; unreadable/disappearing/malformed files
 * produce an incomplete report. At most 4096 directory entries are examined.
 * True means a report was produced, including incomplete reports. Recheck after
 * rotation; this is a point-in-time access observation, never a snapshot or proof
 * that sd-journal will not later skip damaged content. Atomic owned-free output. */
bool tired_journal_access_check(const char *const roots[2], const char *machine_id, bool user_scope,
                                uid_t uid, TiredJournalAccess *output, TiredError *error);
#endif
