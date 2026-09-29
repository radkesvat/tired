#include "tired/journal_stream.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
typedef struct
{
    int count, position, fail, change, calls;
} Fixture;
static int seek_tail(void *context)
{
    Fixture *f = context;
    ++f->calls;
    f->position = f->count;
    return f->fail == 1 ? -EACCES : 0;
}
static int previous(void *context, uint64_t count)
{
    Fixture *f = context;
    ++f->calls;
    if (f->fail == 2)
        return -EIO;
    if (count > (uint64_t)f->count)
        count = (uint64_t)f->count;
    if (count != 0)
        f->position = f->count - (int)count;
    return (int)count;
}
static int next(void *context)
{
    Fixture *f = context;
    ++f->calls;
    if (f->fail == 3)
        return -EIO;
    if (f->position + 1 >= f->count)
        return 0;
    ++f->position;
    return 1;
}
static int seek_head(void *context)
{
    Fixture *f = context;
    ++f->calls;
    f->position = -1;
    return f->fail == 9 ? -EIO : 0;
}
static bool read_record(void *context, TiredJournalRecord *out, TiredError *error)
{
    Fixture *f = context;
    ++f->calls;
    if (f->fail == 4)
        return tired_error_set(error, TIRED_AUTHORIZATION, "fixture-read", "Denied.", EACCES);
    char cursor[32];
    int n = snprintf(cursor, sizeof(cursor), "record-%d", f->position);
    if (!tired_text_set(&out->cursor, cursor, (size_t)n, 32, error))
        return false;
    out->realtime_usec = (uint64_t)(f->position + 1) * 100;
    return true;
}
static int fd(void *context)
{
    Fixture *f = context;
    return f->fail == 5 ? -EPERM : 12;
}
static int events(void *context)
{
    Fixture *f = context;
    return f->fail == 6 ? -EIO : POLLIN;
}
static int timeout(void *context, uint64_t *out)
{
    Fixture *f = context;
    *out = UINT64_MAX;
    return f->fail == 7 ? -EIO : 0;
}
static int process(void *context)
{
    Fixture *f = context;
    return f->fail == 8 ? -EIO : f->change;
}
int main(void)
{
    int result = 1;
    Fixture f = {.count = 5};
    TiredJournalStreamSource source = {.context = &f,
                                       .seek_tail = seek_tail,
                                       .seek_head = seek_head,
                                       .previous_skip = previous,
                                       .next = next,
                                       .read = read_record,
                                       .fd = fd,
                                       .events = events,
                                       .timeout = timeout,
                                       .process = process};
    TiredJournalStream stream = {0};
    TiredJournalRecord record = {0};
    TiredJournalPoll watch = {0};
    TiredError error = {0};
    sd_journal *journal = NULL;
    char path[] = "journal-stream-XXXXXX";
    char *directory = NULL;
    CHECK(tired_journal_stream_begin(&stream, &source, 3, false, 0, &error));
    CHECK(stream.initial_remaining == 3);
    for (uint64_t t = 300; t <= 500; t += 100)
    {
        CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_RECORD);
        CHECK(record.realtime_usec == t);
    }
    f.count = 6;
    CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_END);
    CHECK(record.realtime_usec == 500);
    CHECK(tired_journal_stream_poll(&stream, &watch, &error));
    CHECK(watch.fd == 12 && watch.events == POLLIN && watch.monotonic_deadline_usec == UINT64_MAX);
    f.change = SD_JOURNAL_APPEND;
    CHECK(tired_journal_stream_process(&stream, &error) == SD_JOURNAL_APPEND);
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 600);
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_END);
    f.change = SD_JOURNAL_INVALIDATE;
    CHECK(tired_journal_stream_process(&stream, &error) == SD_JOURNAL_INVALIDATE);
    f.count = 7;
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 700);
    stream = (TiredJournalStream){0};
    CHECK(tired_journal_stream_begin(&stream, &source, 0, false, 0, &error));
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_END);
    ++f.count;
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 800);
    stream = (TiredJournalStream){0};
    CHECK(tired_journal_stream_begin(&stream, &source, 100, true, 700, &error));
    for (int i = 0; i < 6; ++i)
        CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_SKIPPED);
    CHECK(record.realtime_usec == 800); /* Skips do not replace caller output. */
    CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 700); /* Inclusive lower bound. */
    CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_END);
    stream = (TiredJournalStream){0};
    f.count = 0;
    CHECK(tired_journal_stream_begin(&stream, &source, 100, false, 0, &error));
    CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_END);
    f.count = 3;
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 100);
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 200);
    CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_RECORD);
    CHECK(record.realtime_usec == 300);
    stream = (TiredJournalStream){0};
    int saved_calls = f.calls;
    CHECK(!tired_journal_stream_begin(&stream, &source, TIRED_JOURNAL_TAIL_LIMIT + 1, false, 0,
                                      &error));
    CHECK(!stream.started && f.calls == saved_calls);
    for (int stage = 1; stage <= 9; ++stage)
    {
        stream = (TiredJournalStream){0};
        f.count = stage == 9 ? 0 : 2;
        f.fail = stage;
        bool ok = tired_journal_stream_begin(&stream, &source, 1, false, 0, &error);
        if (stage <= 2 || stage == 9)
            CHECK(!ok);
        else
        {
            CHECK(ok);
            if (stage == 3)
            {
                CHECK(tired_journal_stream_next(&stream, false, &record, &error) ==
                      TIRED_JOURNAL_RECORD);
                CHECK(tired_journal_stream_next(&stream, true, &record, &error) ==
                      TIRED_JOURNAL_ERROR);
            }
            else if (stage == 4)
                CHECK(tired_journal_stream_next(&stream, false, &record, &error) ==
                      TIRED_JOURNAL_ERROR);
            else if (stage <= 7)
            {
                watch.fd = 99;
                CHECK(!tired_journal_stream_poll(&stream, &watch, &error));
                CHECK(watch.fd == 99);
            }
            else
                CHECK(tired_journal_stream_process(&stream, &error) < 0);
        }
        CHECK(stream.failed && error.system_errno != 0);
        saved_calls = f.calls;
        CHECK(tired_journal_stream_next(&stream, true, &record, &error) == TIRED_JOURNAL_ERROR);
        CHECK(f.calls == saved_calls);
    }
    directory = mkdtemp(path);
    CHECK(directory != NULL);
    CHECK(sd_journal_open_directory(&journal, directory, 0) >= 0);
    stream = (TiredJournalStream){0};
    CHECK(tired_journal_stream_native(&stream, journal, 100, false, 0, &error));
    CHECK(tired_journal_stream_next(&stream, false, &record, &error) == TIRED_JOURNAL_END);
    CHECK(tired_journal_stream_poll(&stream, &watch, &error));
    CHECK(watch.fd >= 0);
    CHECK(tired_journal_stream_process(&stream, &error) >= 0);
    result = 0;
cleanup:
    tired_journal_record_destroy(&record);
    sd_journal_close(journal);
    if (directory != NULL)
        rmdir(directory);
    return result;
}
