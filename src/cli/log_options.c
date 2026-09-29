#include "tired/log_options.h"
#include <assert.h>
#include <string.h>

static bool invalid(TiredError *error)
{
    return tired_error_set(
        error, TIRED_INVALID, "logs-since",
        "Use @SECONDS[.ffffff] or YYYY-MM-DDTHH:MM:SS[.ffffff]Z since the Unix epoch.", 0);
}
static bool digits(const char *text, size_t n, uint64_t *out)
{
    uint64_t value = 0;
    if (n == 0)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        if (text[i] < '0' || text[i] > '9' || value > (UINT64_MAX - (unsigned)(text[i] - '0')) / 10)
            return false;
        value = value * 10 + (unsigned)(text[i] - '0');
    }
    *out = value;
    return true;
}
static bool fraction(const char *text, size_t n, uint64_t *out)
{
    if (n == 0)
    {
        *out = 0;
        return true;
    }
    if (n < 2 || n > 7 || text[0] != '.' || !digits(text + 1, n - 1, out))
        return false;
    for (size_t i = n - 1; i < 6; ++i)
        *out *= 10;
    return true;
}
static bool leap(uint64_t year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
bool tired_log_since_parse(const char *input, uint64_t *usec, TiredError *error)
{
    assert(input != NULL && usec != NULL);
    size_t length = strnlen(input, 40);
    uint64_t seconds = 0, micros = 0;
    if (length == 0 || length >= 40)
        return invalid(error);
    if (input[0] == '@')
    {
        size_t end = 1;
        while (end < length && input[end] != '.')
            ++end;
        if (!digits(input + 1, end - 1, &seconds) || !fraction(input + end, length - end, &micros))
            return invalid(error);
    }
    else
    {
        uint64_t year, month, day, hour, minute, second;
        if (length < 20 || input[4] != '-' || input[7] != '-' || input[10] != 'T' ||
            input[13] != ':' || input[16] != ':' || input[length - 1] != 'Z' ||
            !digits(input, 4, &year) || !digits(input + 5, 2, &month) ||
            !digits(input + 8, 2, &day) || !digits(input + 11, 2, &hour) ||
            !digits(input + 14, 2, &minute) || !digits(input + 17, 2, &second) ||
            !fraction(input + 19, length - 20, &micros) || year < 1970 || month < 1 || month > 12 ||
            hour > 23 || minute > 59 || second > 59)
            return invalid(error);
        static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        uint64_t month_days = days[month - 1] + (month == 2 && leap(year) ? 1U : 0U);
        if (day < 1 || day > month_days)
            return invalid(error);
        uint64_t elapsed = day - 1;
        for (uint64_t y = 1970; y < year; ++y)
            elapsed += leap(y) ? 366U : 365U;
        for (uint64_t m = 1; m < month; ++m)
            elapsed += days[m - 1] + (m == 2 && leap(year) ? 1U : 0U);
        seconds = elapsed * 86400U + hour * 3600U + minute * 60U + second;
    }
    if (seconds > (UINT64_MAX - micros) / 1000000U)
        return invalid(error);
    *usec = seconds * 1000000U + micros;
    tired_error_clear(error);
    return true;
}
bool tired_log_boot_parse(const char *input, TiredLogOptions *options, TiredError *error)
{
    assert(input != NULL && options != NULL);
    char boot[33] = {0};
    bool current = strcmp(input, "current") == 0;
    if (!current)
    {
        if (strnlen(input, 33) != 32)
            goto invalid_boot;
        for (size_t i = 0; i < 32; ++i)
        {
            char c = input[i];
            if (c >= 'A' && c <= 'F')
                c = (char)(c - 'A' + 'a');
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                goto invalid_boot;
            boot[i] = c;
        }
    }
    options->boot_current = current;
    memcpy(options->boot_id, boot, sizeof(boot));
    tired_error_clear(error);
    return true;
invalid_boot:
    return tired_error_set(error, TIRED_INVALID, "logs-boot",
                           "Use current or a 32-digit hexadecimal boot ID.", 0);
}
