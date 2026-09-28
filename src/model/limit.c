#include "tired/limit.h"

#include <assert.h>
#include <string.h>

bool tired_parse_limit(const char *text, size_t length, bool bytes, TiredLimit *result,
                       TiredError *error)
{
    assert(result != NULL && (text != NULL || length == 0));
    if (length == 8 && memcmp(text, "infinity", 8) == 0)
    {
        *result = (TiredLimit){.infinity = true};
        tired_error_clear(error);
        return true;
    }
    uint64_t multiplier = 1;
    if (bytes && length != 0)
    {
        const char *suffixes = "KMGTPE";
        const char *suffix = text[length - 1] == '\0' ? NULL : strchr(suffixes, text[length - 1]);
        if (suffix != NULL)
        {
            for (size_t i = 0; i <= (size_t)(suffix - suffixes); ++i)
                multiplier *= 1024;
            --length;
        }
    }
    uint64_t value;
    /* Reserve UINT64_MAX for the platform infinity sentinel; never reinterpret it. */
    if (!tired_parse_u64(text, length, 0, (UINT64_MAX - 1) / multiplier, &value, error))
        return false;
    *result = (TiredLimit){.value = value * multiplier};
    tired_error_clear(error);
    return true;
}

bool tired_parse_mode(const char *text, size_t length, uint32_t maximum, uint32_t *mode,
                      TiredError *error)
{
    assert(mode != NULL && (text != NULL || length == 0));
    if (length == 0 || length > 4)
        goto invalid;
    uint32_t value = 0;
    for (size_t i = 0; i < length; ++i)
    {
        if (text[i] < '0' || text[i] > '7')
            goto invalid;
        value = value * 8 + (uint32_t)(text[i] - '0');
    }
    if (value > maximum)
        goto invalid;
    *mode = value;
    tired_error_clear(error);
    return true;
invalid:
    return tired_error_set(error, TIRED_INVALID, "permission-mode",
                           "Expected an octal permission mode within the field range.", 0);
}

bool tired_parse_quota(const char *text, size_t length, uint64_t *quota, TiredError *error)
{
    assert(quota != NULL && (text != NULL || length == 0));
    if (length < 2 || text[length - 1] != '%')
        goto invalid;
    --length;
    size_t dot = 0;
    while (dot < length && text[dot] != '.')
        ++dot;
    uint64_t whole;
    if (!tired_parse_u64(text, dot, 0, (UINT64_MAX - 99) / 100, &whole, error))
        return false;
    uint64_t fraction = 0;
    if (dot != length)
    {
        size_t digits = length - dot - 1;
        if (digits == 0 || digits > 2)
            goto invalid;
        if (!tired_parse_u64(text + dot + 1, digits, 0, 99, &fraction, error))
            return false;
        if (digits == 1)
            fraction *= 10;
    }
    uint64_t value = whole * 100 + fraction;
    if (value == 0)
        goto invalid;
    *quota = value;
    tired_error_clear(error);
    return true;
invalid:
    return tired_error_set(error, TIRED_INVALID, "cpu-quota",
                           "Expected a positive percentage with at most two fractional digits.", 0);
}

bool tired_limit_le(TiredLimit left, TiredLimit right)
{
    return right.infinity || (!left.infinity && left.value <= right.value);
}
