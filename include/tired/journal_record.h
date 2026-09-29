#ifndef TIRED_JOURNAL_RECORD_H
#define TIRED_JOURNAL_RECORD_H
#include "tired/value.h"
#include <systemd/sd-journal.h>
#define TIRED_JOURNAL_MESSAGE_LIMIT (64U * 1024U)
typedef struct
{
    unsigned char *data;
    size_t length;
    bool present, truncated;
} TiredJournalBytes;
typedef struct
{
    uint64_t value;
    bool known, invalid;
} TiredJournalNumber;
typedef struct
{
    TiredText cursor;
    uint64_t realtime_usec, monotonic_usec;
    char boot_id[33];
    TiredJournalBytes message, identifier;
    TiredJournalNumber pid, uid, priority;
} TiredJournalRecord;
/* Injectable native-read boundary. Data includes FIELD= and expires on the next
 * callback. cursor returns a malloc-owned string. Return native negative errno
 * on failure; -ENOENT means an absent optional field only in the data callback. */
typedef struct
{
    void *context;
    int (*threshold)(void *, size_t);
    int (*data)(void *, const char *, const void **, size_t *);
    int (*cursor)(void *, char **);
    int (*realtime)(void *, uint64_t *);
    int (*monotonic)(void *, uint64_t *, sd_id128_t *);
} TiredJournalRecordSource;
/* Decode the current entry; does not advance or change filters. Caller already
 * established a scoped current entry. Copies MESSAGE <=64 KiB and identifier
 * <=256 bytes, retaining binary/NUL and explicit truncation/presence. Cursor is
 * <=4096 bytes. Invalid optional numbers are marked, not guessed as zero. Native
 * read failures propagate, never masquerading as absent or empty messages.
 * Atomic owned output. Data threshold is set to MESSAGE bound + prefix + 1. */
bool tired_journal_record_decode(const TiredJournalRecordSource *source, TiredJournalRecord *output,
                                 TiredError *error);
bool tired_journal_record_read(sd_journal *journal, TiredJournalRecord *output, TiredError *error);
void tired_journal_record_destroy(TiredJournalRecord *record);
#endif
