#include "tired/journal_stream.h"
#include <assert.h>
#include <errno.h>

static bool failure(TiredJournalStream *stream, int rc, TiredError *error)
{
    stream->failed = true;
    TiredStatus status = rc == -EACCES || rc == -EPERM                 ? TIRED_AUTHORIZATION
                         : rc == -ENOMEM                               ? TIRED_INTERNAL
                         : rc == -EOPNOTSUPP || rc == -EPROTONOSUPPORT ? TIRED_UNSUPPORTED
                                                                       : TIRED_RUNTIME_FAILED;
    return tired_error_set(error, status, "journal-iteration",
                           "Cannot iterate or watch the selected journal.", -rc);
}
static bool usable(TiredJournalStream *stream, TiredError *error)
{
    assert(stream != NULL && stream->started);
    if (stream->failed)
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "journal-stream-failed",
                               "Discard the journal stream after a read or watch failure.", 0);
    return true;
}
bool tired_journal_stream_begin(TiredJournalStream *stream, const TiredJournalStreamSource *source,
                                size_t lines, bool since_known, uint64_t since_usec,
                                TiredError *error)
{
    assert(stream != NULL && !stream->started && source != NULL && source->seek_tail != NULL &&
           source->seek_head != NULL && source->previous_skip != NULL && source->next != NULL &&
           source->read != NULL && source->fd != NULL && source->events != NULL &&
           source->timeout != NULL && source->process != NULL);
    if (lines > TIRED_JOURNAL_TAIL_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "journal-tail-limit",
                               "Journal tail exceeds 10000 records.", 0);
    *stream = (TiredJournalStream){
        .source = *source, .started = true, .since_known = since_known, .since_usec = since_usec};
    int rc = source->seek_tail(source->context);
    if (rc < 0)
        return failure(stream, rc, error);
    /* Anchor even a zero-line follow request to a real last entry when possible. */
    rc = source->previous_skip(source->context, lines == 0 ? 1 : lines);
    if (rc < 0)
        return failure(stream, rc, error);
    if ((size_t)rc > (lines == 0 ? 1 : lines))
        return failure(stream, -EBADMSG, error);
    stream->initial_remaining = lines == 0 ? 0 : (size_t)rc;
    stream->current_pending = stream->initial_remaining != 0;
    if (rc == 0)
    {
        /* An unresolved tail would select the newest entry on the next call,
         * losing earlier records from the first batch appended to an empty view. */
        rc = source->seek_head(source->context);
        if (rc < 0)
            return failure(stream, rc, error);
    }
    tired_error_clear(error);
    return true;
}
TiredJournalStep tired_journal_stream_next(TiredJournalStream *stream, bool follow,
                                           TiredJournalRecord *output, TiredError *error)
{
    assert(output != NULL);
    if (!usable(stream, error))
        return TIRED_JOURNAL_ERROR;
    tired_error_clear(error);
    if (!follow && stream->initial_remaining == 0)
        return TIRED_JOURNAL_END;
    if (!stream->current_pending)
    {
        int rc = stream->source.next(stream->source.context);
        if (rc < 0)
        {
            failure(stream, rc, error);
            return TIRED_JOURNAL_ERROR;
        }
        if (rc == 0)
        {
            stream->initial_remaining = 0;
            return TIRED_JOURNAL_END;
        }
        if (rc != 1)
        {
            failure(stream, -EBADMSG, error);
            return TIRED_JOURNAL_ERROR;
        }
    }
    stream->current_pending = false;
    TiredJournalRecord record = {0};
    if (!stream->source.read(stream->source.context, &record, error))
    {
        tired_journal_record_destroy(&record);
        stream->failed = true;
        return TIRED_JOURNAL_ERROR;
    }
    if (stream->initial_remaining != 0)
        --stream->initial_remaining;
    if (stream->since_known && record.realtime_usec < stream->since_usec)
    {
        tired_journal_record_destroy(&record);
        return TIRED_JOURNAL_SKIPPED;
    }
    tired_journal_record_destroy(output);
    *output = record;
    return TIRED_JOURNAL_RECORD;
}
bool tired_journal_stream_poll(TiredJournalStream *stream, TiredJournalPoll *output,
                               TiredError *error)
{
    assert(output != NULL);
    if (!usable(stream, error))
        return false;
    TiredJournalPoll poll = {0};
    poll.fd = stream->source.fd(stream->source.context);
    if (poll.fd < 0)
        return failure(stream, poll.fd, error);
    poll.events = stream->source.events(stream->source.context);
    if (poll.events < 0)
        return failure(stream, poll.events, error);
    int rc = stream->source.timeout(stream->source.context, &poll.monotonic_deadline_usec);
    if (rc < 0)
        return failure(stream, rc, error);
    *output = poll;
    tired_error_clear(error);
    return true;
}
int tired_journal_stream_process(TiredJournalStream *stream, TiredError *error)
{
    if (!usable(stream, error))
        return -1;
    int rc = stream->source.process(stream->source.context);
    if (rc < 0)
    {
        failure(stream, rc, error);
        return -1;
    }
    if (rc != SD_JOURNAL_NOP && rc != SD_JOURNAL_APPEND && rc != SD_JOURNAL_INVALIDATE)
    {
        failure(stream, -EBADMSG, error);
        return -1;
    }
    tired_error_clear(error);
    return rc;
}
static int seek_tail(void *context) { return sd_journal_seek_tail(context); }
static int seek_head(void *context) { return sd_journal_seek_head(context); }
static int previous_skip(void *context, uint64_t count)
{
    return sd_journal_previous_skip(context, count);
}
static int next(void *context) { return sd_journal_next(context); }
static bool read_record(void *context, TiredJournalRecord *out, TiredError *error)
{
    return tired_journal_record_read(context, out, error);
}
static int fd(void *context) { return sd_journal_get_fd(context); }
static int events(void *context) { return sd_journal_get_events(context); }
static int timeout(void *context, uint64_t *out) { return sd_journal_get_timeout(context, out); }
static int process(void *context) { return sd_journal_process(context); }
bool tired_journal_stream_native(TiredJournalStream *stream, sd_journal *journal, size_t lines,
                                 bool since_known, uint64_t since_usec, TiredError *error)
{
    assert(journal != NULL);
    const TiredJournalStreamSource source = {.context = journal,
                                             .seek_tail = seek_tail,
                                             .seek_head = seek_head,
                                             .previous_skip = previous_skip,
                                             .next = next,
                                             .read = read_record,
                                             .fd = fd,
                                             .events = events,
                                             .timeout = timeout,
                                             .process = process};
    return tired_journal_stream_begin(stream, &source, lines, since_known, since_usec, error);
}
