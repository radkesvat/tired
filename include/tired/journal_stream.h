#ifndef TIRED_JOURNAL_STREAM_H
#define TIRED_JOURNAL_STREAM_H
#include "tired/journal_record.h"
#define TIRED_JOURNAL_TAIL_LIMIT 10000U
typedef struct
{
    void *context;
    int (*seek_tail)(void *);
    int (*seek_head)(void *);
    int (*previous_skip)(void *, uint64_t);
    int (*next)(void *);
    bool (*read)(void *, TiredJournalRecord *, TiredError *);
    int (*fd)(void *);
    int (*events)(void *);
    int (*timeout)(void *, uint64_t *);
    int (*process)(void *);
} TiredJournalStreamSource;
typedef struct
{
    TiredJournalStreamSource source;
    size_t initial_remaining;
    uint64_t since_usec;
    bool started, failed, current_pending, since_known;
} TiredJournalStream;
typedef enum
{
    TIRED_JOURNAL_ERROR = -1,
    TIRED_JOURNAL_END,
    TIRED_JOURNAL_RECORD,
    TIRED_JOURNAL_SKIPPED
} TiredJournalStep;
typedef struct
{
    int fd, events;
    uint64_t monotonic_deadline_usec; /* UINT64_MAX means no journal deadline. */
} TiredJournalPoll;
/* Fresh zeroed stream; exclusive borrowed source already scoped by selection.
 * No opening, access authorization, or filter changes. Source context must outlive
 * stream. Seek newest lines (0..10000), then emit in journal order. Zero lines
 * positions after the current last record. No record buffer is retained here.
 * Failures poison the stream: discard it and its native handle, never retry past
 * unread data. Native library calls can perform IO; this is not a hard IO deadline. */
bool tired_journal_stream_begin(TiredJournalStream *stream, const TiredJournalStreamSource *source,
                                size_t lines, bool since_known, uint64_t since_usec,
                                TiredError *error);
bool tired_journal_stream_native(TiredJournalStream *stream, sd_journal *journal, size_t lines,
                                 bool since_known, uint64_t since_usec, TiredError *error);
/* At most one entry decoded per call. SKIPPED means before the inclusive since
 * bound. Without follow, stops after the initial tail count. With follow, END is
 * temporary exhaustion: poll/process then continue. Output unchanged except on
 * RECORD. Caller owns output and must destroy it. No snapshot guarantee during
 * concurrent rotation/vacuum; initial count may shrink. */
TiredJournalStep tired_journal_stream_next(TiredJournalStream *stream, bool follow,
                                           TiredJournalRecord *output, TiredError *error);
/* Integrate with terminal/signals in caller's poll loop. Never close returned fd.
 * Refresh poll information each wait and process after each wake/deadline. Process
 * returns native NOP/APPEND/INVALIDATE; INVALIDATE explicitly reports rotation or
 * other file-set changes. Sequential follow continues from its current position;
 * it does not replay entries inserted before it or claim a lossless history. */
bool tired_journal_stream_poll(TiredJournalStream *stream, TiredJournalPoll *output,
                               TiredError *error);
int tired_journal_stream_process(TiredJournalStream *stream, TiredError *error);
#endif
