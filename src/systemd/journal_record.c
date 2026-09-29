#include "tired/journal_record.h"
#include <assert.h>
#include <errno.h>
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>

static void bytes_destroy(TiredJournalBytes *bytes)
{
    if (bytes->data != NULL)
        OPENSSL_cleanse(bytes->data, bytes->length);
    free(bytes->data);
    *bytes = (TiredJournalBytes){0};
}
void tired_journal_record_destroy(TiredJournalRecord *record)
{
    if (record == NULL)
        return;
    bytes_destroy(&record->message);
    bytes_destroy(&record->identifier);
    tired_text_destroy(&record->cursor);
    *record = (TiredJournalRecord){0};
}
static bool native_error(int rc, TiredError *error)
{
    TiredStatus status = rc == -EACCES || rc == -EPERM                 ? TIRED_AUTHORIZATION
                         : rc == -ENOMEM                               ? TIRED_INTERNAL
                         : rc == -EPROTONOSUPPORT || rc == -EOPNOTSUPP ? TIRED_UNSUPPORTED
                                                                       : TIRED_RUNTIME_FAILED;
    return tired_error_set(error, status, "journal-record-read",
                           "Cannot read the current journal entry.", -rc);
}
static bool field(const TiredJournalRecordSource *source, const char *name, const void **data,
                  size_t *length, bool *present, TiredError *error)
{
    const void *raw = NULL;
    size_t size = 0;
    int rc = source->data(source->context, name, &raw, &size);
    if (rc == -ENOENT)
    {
        *present = false;
        *data = NULL;
        *length = 0;
        return true;
    }
    if (rc < 0)
        return native_error(rc, error);
    size_t prefix = strlen(name);
    if (raw == NULL || size < prefix + 1 || memcmp(raw, name, prefix) != 0 ||
        ((const char *)raw)[prefix] != '=')
        return tired_error_set(error, TIRED_INVALID, "journal-field-shape",
                               "Journal field has an invalid prefix.", 0);
    *data = (const char *)raw + prefix + 1;
    *length = size - prefix - 1;
    *present = true;
    return true;
}
static bool bytes_read(const TiredJournalRecordSource *source, const char *name, size_t limit,
                       TiredJournalBytes *bytes, TiredError *error)
{
    const void *data;
    size_t length;
    if (!field(source, name, &data, &length, &bytes->present, error))
        return false;
    if (!bytes->present)
        return true;
    bytes->length = length < limit ? length : limit;
    bytes->truncated = length > limit;
    bytes->data = malloc(bytes->length + 1);
    if (bytes->data == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot copy journal field bytes.", 0);
    memcpy(bytes->data, data, bytes->length);
    bytes->data[bytes->length] = 0; /* Convenience only; embedded NUL remains valid. */
    return true;
}
static bool number_read(const TiredJournalRecordSource *source, const char *name, uint64_t maximum,
                        TiredJournalNumber *number, TiredError *error)
{
    const void *data;
    size_t length;
    bool present;
    if (!field(source, name, &data, &length, &present, error))
        return false;
    if (!present)
        return true;
    TiredError invalid = {0};
    number->known =
        length <= 20 && tired_parse_u64(data, length, 0, maximum, &number->value, &invalid);
    number->invalid = !number->known;
    return true;
}
bool tired_journal_record_decode(const TiredJournalRecordSource *source, TiredJournalRecord *output,
                                 TiredError *error)
{
    assert(source != NULL && output != NULL && source->threshold != NULL && source->data != NULL &&
           source->cursor != NULL && source->realtime != NULL && source->monotonic != NULL);
    TiredJournalRecord record = {0};
    char *cursor = NULL;
    bool ok = false;
    int rc = source->threshold(source->context, TIRED_JOURNAL_MESSAGE_LIMIT + sizeof("MESSAGE="));
    if (rc < 0)
        goto native_failed;
    rc = source->cursor(source->context, &cursor);
    if (rc < 0)
        goto native_failed;
    size_t length = cursor == NULL ? 0 : strnlen(cursor, 4097);
    if (length == 0 || length > 4096)
    {
        tired_error_set(error, TIRED_INVALID, "journal-cursor",
                        "Journal cursor is missing or exceeds its bound.", 0);
        goto done;
    }
    if (!tired_text_set(&record.cursor, cursor, length, 4096, error))
        goto done;
    rc = source->realtime(source->context, &record.realtime_usec);
    if (rc < 0)
        goto native_failed;
    sd_id128_t boot;
    rc = source->monotonic(source->context, &record.monotonic_usec, &boot);
    if (rc < 0)
        goto native_failed;
    sd_id128_to_string(boot, record.boot_id);
    if (!bytes_read(source, "MESSAGE", TIRED_JOURNAL_MESSAGE_LIMIT, &record.message, error) ||
        !bytes_read(source, "SYSLOG_IDENTIFIER", 256, &record.identifier, error) ||
        !number_read(source, "_PID", INT32_MAX, &record.pid, error) ||
        !number_read(source, "_UID", UINT32_MAX - 1U, &record.uid, error) ||
        !number_read(source, "PRIORITY", 7, &record.priority, error))
        goto done;
    tired_journal_record_destroy(output);
    *output = record;
    record = (TiredJournalRecord){0};
    tired_error_clear(error);
    ok = true;
    goto done;
native_failed:
    native_error(rc, error);
done:
    free(cursor);
    tired_journal_record_destroy(&record);
    return ok;
}
static int threshold(void *context, size_t size)
{
    return sd_journal_set_data_threshold(context, size);
}
static int data(void *context, const char *name, const void **bytes, size_t *length)
{
    return sd_journal_get_data(context, name, bytes, length);
}
static int cursor(void *context, char **value) { return sd_journal_get_cursor(context, value); }
static int realtime(void *context, uint64_t *value)
{
    return sd_journal_get_realtime_usec(context, value);
}
static int monotonic(void *context, uint64_t *value, sd_id128_t *boot)
{
    return sd_journal_get_monotonic_usec(context, value, boot);
}
bool tired_journal_record_read(sd_journal *journal, TiredJournalRecord *output, TiredError *error)
{
    assert(journal != NULL);
    const TiredJournalRecordSource source = {.context = journal,
                                             .threshold = threshold,
                                             .data = data,
                                             .cursor = cursor,
                                             .realtime = realtime,
                                             .monotonic = monotonic};
    return tired_journal_record_decode(&source, output, error);
}
