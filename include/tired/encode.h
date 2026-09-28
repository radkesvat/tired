#ifndef TIRED_ENCODE_H
#define TIRED_ENCODE_H
#include "tired/value.h"

#define TIRED_UNIT_LIMIT (4U * TIRED_INPUT_LIMIT)

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    size_t limit;
} TiredBuffer;
/* Initialize with a fixed limit. Input must not alias the buffer allocation.
 * Appends preserve the buffer on failure and always terminate it with NUL. */
void tired_buffer_init(TiredBuffer *buffer, size_t limit);
bool tired_buffer_append(TiredBuffer *buffer, const char *data, size_t length, TiredError *error);
void tired_buffer_destroy(TiredBuffer *buffer);
/* Transfer allocation into an owned text destination, resetting buffer. */
bool tired_buffer_take(TiredBuffer *buffer, TiredText *output, TiredError *error);

/* A whole quoted systemd command/list token. Percent specifiers are escaped.
 * Command callers MUST use the ':' ExecStart prefix to disable $ expansion.
 * Not appropriate for directives that do not implement C-style unquoting. */
bool tired_encode_token(const char *data, size_t length, TiredText *output, TiredError *error);
/* Scalar directives using literal text plus specifier expansion. Reject controls,
 * leading/trailing whitespace, and trailing backslash (line continuation).
 * Quotes and backslashes elsewhere are literal, not shell or C syntax. */
bool tired_encode_directive(const char *data, size_t length, TiredText *output, TiredError *error);
/* Plain terminal/comment text: UTF-8 retained; controls become visible escapes.
 * Does not perform unit-file escaping and is never an executable serialization. */
bool tired_encode_display(const char *data, size_t length, TiredText *output, TiredError *error);
#endif
