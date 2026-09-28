#ifndef TIRED_LIMIT_H
#define TIRED_LIMIT_H

#include "tired/value.h"

typedef struct
{
    bool infinity;
    uint64_t value;
} TiredLimit;
/* Decimal integer or infinity; byte quantities optionally accept K/M/G/T/P/E
 * binary multipliers. Output remains unchanged on failure. */
bool tired_parse_limit(const char *text, size_t length, bool bytes, TiredLimit *result,
                       TiredError *error);
bool tired_parse_mode(const char *text, size_t length, uint32_t maximum, uint32_t *mode,
                      TiredError *error);
/* Percent with at most two decimal places; stored in hundredths of a percent.
 * Values above 100% intentionally represent more than one CPU. */
bool tired_parse_quota(const char *text, size_t length, uint64_t *quota, TiredError *error);
bool tired_limit_le(TiredLimit left, TiredLimit right);

#endif
