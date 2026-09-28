#ifndef TIRED_VALUE_H
#define TIRED_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TIRED_INPUT_LIMIT (1024U * 1024U)
#define TIRED_ARGUMENT_LIMIT 4096U

typedef enum
{
    TIRED_OK = 0,
    TIRED_INTERNAL = 1,
    TIRED_INVALID = 2,
    TIRED_UNSUPPORTED = 3,
    TIRED_AUTHORIZATION = 4,
    TIRED_CONFLICT = 5,
    TIRED_ROLLED_BACK = 6,
    TIRED_RUNTIME_FAILED = 7,
    TIRED_RECOVERY_REQUIRED = 8,
    TIRED_NOT_FOUND = 9,
    TIRED_CANCELLED = 10,
    TIRED_INTERRUPTED = 130
} TiredStatus;

typedef struct
{
    TiredStatus status;
    const char *code;
    const char *message;
    int system_errno;
} TiredError;

/* Error strings are static, safe messages, never borrowed external input. */
void tired_error_clear(TiredError *error);
bool tired_error_set(TiredError *error, TiredStatus status, const char *code, const char *message,
                     int system_errno);

typedef struct
{
    char *data;
    size_t length;
} TiredText;

/* Initialize owned values to zero. Setters copy borrowed input, reject NUL, and
 * leave the destination unchanged on failure. Destroy is safe to repeat.
 * An allocated empty string differs from an unset (NULL) string. */
bool tired_text_set(TiredText *text, const char *data, size_t length, size_t limit,
                    TiredError *error);
void tired_text_destroy(TiredText *text);

typedef struct
{
    TiredText *items;
    size_t count;
    size_t bytes;
} TiredTextList;

/* bytes includes one terminator per item, so empty arguments consume budget.
 * Each append copies input. The list owns all items until destroy. */
bool tired_text_list_append(TiredTextList *list, const char *data, size_t length,
                            size_t count_limit, size_t byte_limit, TiredError *error);
void tired_text_list_destroy(TiredTextList *list);

/* Strict ASCII decimal integers: no whitespace, sign, suffix, or embedded NUL.
 * Output is untouched on failure. Bounds are inclusive. */
bool tired_parse_u64(const char *data, size_t length, uint64_t minimum, uint64_t maximum,
                     uint64_t *value, TiredError *error);
bool tired_parse_i64(const char *data, size_t length, int64_t minimum, int64_t maximum,
                     int64_t *value, TiredError *error);

#endif
