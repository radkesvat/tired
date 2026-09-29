#ifndef TIRED_LOG_OPTIONS_H
#define TIRED_LOG_OPTIONS_H
#include "tired/value.h"
typedef struct
{
    bool follow, lines_set, since_set, boot_set, boot_current;
    size_t lines; /* Unset means use the configured default at execution time. */
    uint64_t since_usec;
    char boot_id[33]; /* Normalized lowercase; empty for current or unset. */
} TiredLogOptions;
/* Pure parsing. since accepts @SECONDS[.ffffff] or UTC
 * YYYY-MM-DDTHH:MM:SS[.ffffff]Z, from the Unix epoch onwards. No locale, timezone,
 * clock access, date normalization, or guessed relative-time units. Atomic output. */
bool tired_log_since_parse(const char *input, uint64_t *usec, TiredError *error);
/* current or a 32-hex boot ID. Does not read the host boot ID during parsing. */
bool tired_log_boot_parse(const char *input, TiredLogOptions *options, TiredError *error);
#endif
