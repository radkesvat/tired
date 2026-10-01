#include "tired/unit_words.h"
#include "tired/encode.h"
#include "tired/memory.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void tired_unit_words_destroy(TiredUnitWords *words)
{
    if (words == NULL)
        return;
    for (size_t i = 0; i < words->count; ++i)
    {
        tired_memory_clear(words->words[i].value.data, words->words[i].value.length);
        tired_text_destroy(&words->words[i].value);
    }
    free(words->words);
    *words = (TiredUnitWords){0};
}
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "unit-word-syntax",
                           "Unit value contains unsupported or malformed quoting or escapes.", 0);
}
static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static int digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static bool escape(const char *data, size_t length, size_t *offset, TiredBuffer *value,
                   TiredError *error)
{
    if (*offset == length)
        return invalid(error);
    char c = data[(*offset)++];
    const char keys[] = "abfnrtv\\\"'s";
    const char values[] = {'\a', '\b', '\f', '\n', '\r', '\t', '\v', '\\', '"', '\'', ' '};
    const char *key = strchr(keys, c);
    if (key != NULL)
        return tired_buffer_append(value, &values[key - keys], 1, error);
    bool unicode = c == 'u' || c == 'U';
    unsigned count = c == 'x' ? 2 : c == 'u' ? 4 : c == 'U' ? 8 : 3;
    unsigned base = c >= '0' && c <= '7' ? 8 : 16;
    if (base == 8)
        --*offset;
    else if (c != 'x' && !unicode)
        return invalid(error);
    if (count > length - *offset)
        return invalid(error);
    uint32_t number = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        int d = digit(data[(*offset)++]);
        if (d < 0 || (unsigned)d >= base)
            return invalid(error);
        number = number * base + (unsigned)d;
    }
    if (number == 0 || (!unicode && number > 255) ||
        (unicode && (number > 0x10ffff || (number >= 0xd800 && number <= 0xdfff) ||
                     (number >= 0xfdd0 && number <= 0xfdef) || (number & 0xfffe) == 0xfffe)))
        return invalid(error);
    char bytes[4];
    size_t size = 1;
    if (!unicode || number < 0x80)
        bytes[0] = (char)number;
    else if (number < 0x800)
    {
        size = 2;
        bytes[0] = (char)(0xc0 | (number >> 6));
        bytes[1] = (char)(0x80 | (number & 63));
    }
    else if (number < 0x10000)
    {
        size = 3;
        bytes[0] = (char)(0xe0 | (number >> 12));
        bytes[1] = (char)(0x80 | ((number >> 6) & 63));
        bytes[2] = (char)(0x80 | (number & 63));
    }
    else
    {
        size = 4;
        bytes[0] = (char)(0xf0 | (number >> 18));
        bytes[1] = (char)(0x80 | ((number >> 12) & 63));
        bytes[2] = (char)(0x80 | ((number >> 6) & 63));
        bytes[3] = (char)(0x80 | (number & 63));
    }
    return tired_buffer_append(value, bytes, size, error);
}
bool tired_unit_words_parse(const char *data, size_t length, TiredUnitWords *output,
                            TiredError *error)
{
    assert((data != NULL || length == 0) && output != NULL);
    if (length > TIRED_UNIT_LIMIT || (length != 0 && memchr(data, '\0', length) != NULL))
        return invalid(error);
    TiredUnitWords result = {0};
    TiredBuffer value;
    tired_buffer_init(&value, TIRED_INPUT_LIMIT);
    size_t offset = 0, bytes = 0, capacity = 0;
    bool ok = false;
    while (offset < length)
    {
        while (offset < length && space(data[offset]))
            ++offset;
        if (offset == length)
            break;
        if (result.count == TIRED_ARGUMENT_LIMIT)
        {
            tired_error_set(error, TIRED_INVALID, "unit-word-count",
                            "Unit value has too many words.", 0);
            goto done;
        }
        size_t start = offset;
        char quote = 0;
        while (offset < length)
        {
            char c = data[offset];
            if (quote == 0 && space(c))
                break;
            ++offset;
            if (c == '\\')
            {
                if (!escape(data, length, &offset, &value, error))
                    goto done;
            }
            else if (quote != 0 && c == quote)
                quote = 0;
            else if (quote == 0 && (c == '\'' || c == '"'))
                quote = c;
            else if (!tired_buffer_append(&value, &c, 1, error))
                goto done;
        }
        if (quote != 0)
        {
            invalid(error);
            goto done;
        }
        if (value.length >= TIRED_INPUT_LIMIT - bytes)
        {
            tired_error_set(error, TIRED_INVALID, "unit-word-bytes",
                            "Decoded unit value exceeds its byte limit.", 0);
            goto done;
        }
        if (result.count == capacity)
        {
            size_t next = capacity == 0 ? 16 : capacity * 2;
            TiredUnitWord *grown = realloc(result.words, next * sizeof(*grown));
            if (grown == NULL)
            {
                tired_error_set(error, TIRED_INTERNAL, "allocation",
                                "Cannot allocate unit value words.", 0);
                goto done;
            }
            result.words = grown;
            capacity = next;
        }
        TiredUnitWord word = {.start = start, .end = offset};
        bytes += value.length + 1;
        if (!tired_buffer_take(&value, &word.value, error))
            goto done;
        result.words[result.count++] = word;
        tired_buffer_init(&value, TIRED_INPUT_LIMIT - bytes);
    }
    tired_unit_words_destroy(output);
    *output = result;
    result = (TiredUnitWords){0};
    tired_error_clear(error);
    ok = true;
done:
    if (value.data != NULL)
        tired_memory_clear(value.data, value.length);
    tired_buffer_destroy(&value);
    tired_unit_words_destroy(&result);
    return ok;
}
