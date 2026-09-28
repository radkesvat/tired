#include "tired/value.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void tired_error_clear(TiredError *error)
{
    if (error != NULL)
        *error = (TiredError){0};
}

bool tired_error_set(TiredError *error, TiredStatus status, const char *code, const char *message,
                     int system_errno)
{
    if (error != NULL)
        *error = (TiredError){status, code, message, system_errno};
    return false;
}

bool tired_text_set(TiredText *text, const char *data, size_t length, size_t limit,
                    TiredError *error)
{
    assert(text != NULL);
    assert(data != NULL || length == 0);
    if (length > limit || length == SIZE_MAX)
        return tired_error_set(error, TIRED_INVALID, "input-limit", "Text exceeds the input limit.",
                               0);
    if (length != 0 && memchr(data, '\0', length) != NULL)
        return tired_error_set(error, TIRED_INVALID, "embedded-nul",
                               "Text contains an unsupported NUL byte.", 0);
    char *copy = malloc(length + 1);
    if (copy == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate text.", errno);
    if (length != 0)
        memcpy(copy, data, length);
    copy[length] = '\0';
    free(text->data);
    *text = (TiredText){copy, length};
    tired_error_clear(error);
    return true;
}

void tired_text_destroy(TiredText *text)
{
    if (text == NULL)
        return;
    free(text->data);
    *text = (TiredText){0};
}

bool tired_text_list_append(TiredTextList *list, const char *data, size_t length,
                            size_t count_limit, size_t byte_limit, TiredError *error)
{
    assert(list != NULL);
    if (list->count >= count_limit || list->count >= SIZE_MAX / sizeof(*list->items) ||
        list->bytes >= byte_limit || length >= byte_limit - list->bytes)
        return tired_error_set(error, TIRED_INVALID, "collection-limit",
                               "Collection exceeds its count or byte limit.", 0);
    TiredText item = {0};
    if (!tired_text_set(&item, data, length, byte_limit - list->bytes - 1, error))
        return false;
    TiredText *items = realloc(list->items, (list->count + 1) * sizeof(*items));
    if (items == NULL)
    {
        int saved_errno = errno;
        tired_text_destroy(&item);
        return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot grow collection.",
                               saved_errno);
    }
    list->items = items;
    list->items[list->count++] = item;
    list->bytes += length + 1;
    tired_error_clear(error);
    return true;
}

void tired_text_list_destroy(TiredTextList *list)
{
    if (list == NULL)
        return;
    for (size_t i = 0; i < list->count; ++i)
        tired_text_destroy(&list->items[i]);
    free(list->items);
    *list = (TiredTextList){0};
}

bool tired_parse_u64(const char *data, size_t length, uint64_t minimum, uint64_t maximum,
                     uint64_t *value, TiredError *error)
{
    assert(data != NULL || length == 0);
    assert(value != NULL);
    assert(minimum <= maximum);
    if (length == 0)
        return tired_error_set(error, TIRED_INVALID, "integer-syntax",
                               "Expected a decimal integer.", 0);
    uint64_t parsed = 0;
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char digit = (unsigned char)data[i];
        if (digit < '0' || digit > '9')
            return tired_error_set(error, TIRED_INVALID, "integer-syntax",
                                   "Expected a decimal integer.", 0);
        uint64_t part = digit - '0';
        if (part > maximum || parsed > (maximum - part) / 10)
            return tired_error_set(error, TIRED_INVALID, "integer-range",
                                   "Integer is outside the allowed range.", 0);
        parsed = parsed * 10 + part;
    }
    if (parsed < minimum)
        return tired_error_set(error, TIRED_INVALID, "integer-range",
                               "Integer is outside the allowed range.", 0);
    *value = parsed;
    tired_error_clear(error);
    return true;
}

bool tired_parse_i64(const char *data, size_t length, int64_t minimum, int64_t maximum,
                     int64_t *value, TiredError *error)
{
    assert(data != NULL || length == 0);
    assert(value != NULL);
    assert(minimum <= maximum);
    bool negative = length != 0 && data[0] == '-';
    size_t offset = negative ? 1 : 0;
    uint64_t magnitude = 0;
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    if (!tired_parse_u64(data == NULL ? NULL : data + offset, length - offset, 0, limit, &magnitude,
                         error))
        return false;
    int64_t parsed;
    if (negative)
        parsed = magnitude == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)magnitude;
    else
        parsed = (int64_t)magnitude;
    if (parsed < minimum || parsed > maximum)
        return tired_error_set(error, TIRED_INVALID, "integer-range",
                               "Integer is outside the allowed range.", 0);
    *value = parsed;
    tired_error_clear(error);
    return true;
}
