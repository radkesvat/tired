#include "tired/journal_record.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    unsigned char scratch[TIRED_JOURNAL_MESSAGE_LIMIT + 64];
    size_t threshold, message_length, cursor_length;
    int failure, fail_stage, monotonic_error;
    bool missing, wrong_prefix, invalid_numbers, large;
    const char *number;
} Fixture;
static int threshold(void *context, size_t size)
{
    Fixture *f = context;
    f->threshold = size;
    return f->fail_stage == 1 ? f->failure : 0;
}
static int cursor(void *context, char **out)
{
    Fixture *f = context;
    if (f->fail_stage == 2)
        return f->failure;
    *out = malloc(f->cursor_length + 1);
    if (*out == NULL)
        return -ENOMEM;
    memset(*out, 'c', f->cursor_length);
    (*out)[f->cursor_length] = 0;
    return 0;
}
static int realtime(void *context, uint64_t *out)
{
    Fixture *f = context;
    *out = 0;
    return f->fail_stage == 3 ? f->failure : 0;
}
static int monotonic(void *context, uint64_t *out, sd_id128_t *boot)
{
    Fixture *f = context;
    *out = 42;
    memset(boot, 0xab, sizeof(*boot));
    return f->monotonic_error;
}
static int data(void *context, const char *name, const void **out, size_t *length)
{
    Fixture *f = context;
    /* Every read invalidates the previous native buffer. */
    memset(f->scratch, 'z', sizeof(f->scratch));
    if (f->fail_stage == 4 && strcmp(name, "SYSLOG_IDENTIFIER") == 0)
        return f->failure;
    if (f->missing)
        return -ENOENT;
    size_t prefix = strlen(name) + 1;
    memcpy(f->scratch, name, prefix - 1);
    f->scratch[prefix - 1] = '=';
    if (f->wrong_prefix)
        f->scratch[0] = '?';
    size_t n;
    if (strcmp(name, "MESSAGE") == 0)
    {
        static const unsigned char binary[] = {'a', 0, 27, 255};
        n = f->message_length;
        if (!f->large)
            memcpy(f->scratch + prefix, binary, n);
    }
    else if (strcmp(name, "SYSLOG_IDENTIFIER") == 0)
    {
        n = 257;
    }
    else
    {
        const char *number = f->number != NULL    ? f->number
                             : f->invalid_numbers ? "18446744073709551616"
                                                  : "0";
        n = strlen(number);
        memcpy(f->scratch + prefix, number, n);
    }
    *out = f->scratch;
    *length = prefix + n;
    if (*length > f->threshold)
        *length = f->threshold;
    return 0;
}
int main(void)
{
    int result = 1;
    Fixture f = {.message_length = 4, .cursor_length = 4};
    TiredJournalRecordSource source = {.context = &f,
                                       .threshold = threshold,
                                       .cursor = cursor,
                                       .realtime = realtime,
                                       .monotonic = monotonic,
                                       .data = data};
    TiredJournalRecord record = {0};
    TiredError error = {0};
    sd_journal *journal = NULL;
    char path[] = "journal-record-XXXXXX";
    char *directory = NULL;
    CHECK(tired_journal_record_decode(&source, &record, &error));
    CHECK(f.threshold == TIRED_JOURNAL_MESSAGE_LIMIT + 9);
    CHECK(record.message.present && !record.message.truncated && record.message.length == 4);
    CHECK(memcmp(record.message.data, "a\0\033\377", 4) == 0);
    CHECK(record.identifier.present && record.identifier.truncated &&
          record.identifier.length == 256);
    CHECK(record.identifier.data[0] == 'z');
    CHECK(record.realtime_usec == 0 && record.monotonic_usec == 42);
    CHECK(strcmp(record.boot_id, "abababababababababababababababab") == 0);
    CHECK(record.pid.known && record.pid.value == 0 && !record.pid.invalid);
    CHECK(record.uid.known && record.priority.known);
    const char *numbers[] = {"7",          "8", "2147483647", "2147483648", "4294967294",
                             "4294967295", "",  "-1",         " 1",         "1x"};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i)
    {
        f.number = numbers[i];
        CHECK(tired_journal_record_decode(&source, &record, &error));
        CHECK(record.priority.known == (i == 0));
        CHECK(record.pid.known == (i <= 2));
        CHECK(record.uid.known == (i <= 4));
        CHECK(record.pid.invalid == !record.pid.known);
        CHECK(record.uid.invalid == !record.uid.known);
        CHECK(record.priority.invalid == !record.priority.known);
    }
    f.number = NULL;
    f.large = true;
    f.message_length = TIRED_JOURNAL_MESSAGE_LIMIT;
    CHECK(tired_journal_record_decode(&source, &record, &error));
    CHECK(record.message.length == TIRED_JOURNAL_MESSAGE_LIMIT && !record.message.truncated);
    ++f.message_length;
    CHECK(tired_journal_record_decode(&source, &record, &error));
    CHECK(record.message.length == TIRED_JOURNAL_MESSAGE_LIMIT && record.message.truncated);
    f.message_length = 0;
    f.invalid_numbers = true;
    CHECK(tired_journal_record_decode(&source, &record, &error));
    CHECK(record.message.present && record.message.length == 0 && record.message.data != NULL);
    CHECK(record.pid.invalid && !record.pid.known && record.uid.invalid && record.priority.invalid);
    f.missing = true;
    CHECK(tired_journal_record_decode(&source, &record, &error));
    CHECK(!record.message.present && record.message.data == NULL && !record.identifier.present);
    CHECK(!record.pid.known && !record.pid.invalid);
    f.missing = false;
    const int failures[] = {-EACCES, -EPERM, -EIO, -EPROTONOSUPPORT, -ENOMEM};
    for (int stage = 1; stage <= 4; ++stage)
        for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
        {
            f.fail_stage = stage;
            f.failure = failures[i];
            char *saved = record.cursor.data;
            CHECK(!tired_journal_record_decode(&source, &record, &error));
            CHECK(record.cursor.data == saved && error.system_errno == -failures[i]);
            CHECK(error.status == (i < 2    ? TIRED_AUTHORIZATION
                                   : i == 2 ? TIRED_RUNTIME_FAILED
                                   : i == 3 ? TIRED_UNSUPPORTED
                                            : TIRED_INTERNAL));
        }
    f.fail_stage = 0;
    f.monotonic_error = -ENOENT;
    CHECK(!tired_journal_record_decode(&source, &record, &error) && error.system_errno == ENOENT);
    f.monotonic_error = -EIO;
    CHECK(!tired_journal_record_decode(&source, &record, &error) && error.system_errno == EIO);
    f.monotonic_error = 0;
    f.wrong_prefix = true;
    CHECK(!tired_journal_record_decode(&source, &record, &error));
    CHECK(strcmp(error.code, "journal-field-shape") == 0);
    f.wrong_prefix = false;
    f.cursor_length = 4097;
    CHECK(!tired_journal_record_decode(&source, &record, &error));
    CHECK(strcmp(error.code, "journal-cursor") == 0);
    f.cursor_length = 0;
    CHECK(!tired_journal_record_decode(&source, &record, &error));
    f.cursor_length = 4096;
    CHECK(tired_journal_record_decode(&source, &record, &error));
    directory = mkdtemp(path);
    CHECK(directory != NULL);
    CHECK(sd_journal_open_directory(&journal, directory, 0) >= 0);
    CHECK(!tired_journal_record_read(journal, &record, &error));
    CHECK(record.cursor.length == 4096);
    result = 0;
cleanup:
    sd_journal_close(journal);
    if (directory != NULL)
        rmdir(directory);
    tired_journal_record_destroy(&record);
    return result;
}
