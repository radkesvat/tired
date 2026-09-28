#include "tired/encode.h"
#include "tired/capture.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void tired_buffer_init(TiredBuffer *buffer, size_t limit)
{
    assert(buffer != NULL && limit < SIZE_MAX);
    *buffer = (TiredBuffer){.limit = limit};
}

bool tired_buffer_append(TiredBuffer *buffer, const char *data, size_t length, TiredError *error)
{
    assert(buffer != NULL && (data != NULL || length == 0));
    if (length > buffer->limit - buffer->length)
        return tired_error_set(error, TIRED_INVALID, "output-limit",
                               "Generated output exceeds its byte limit.", 0);
    size_t required = buffer->length + length + 1;
    if (required > buffer->capacity)
    {
        size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
        if (capacity > buffer->limit + 1)
            capacity = buffer->limit + 1;
        while (capacity < required)
        {
            if (capacity > (buffer->limit + 1) / 2)
            {
                capacity = buffer->limit + 1;
                break;
            }
            capacity *= 2;
        }
        char *grown = realloc(buffer->data, capacity);
        if (grown == NULL)
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot grow output buffer.", errno);
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (length != 0)
        memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    tired_error_clear(error);
    return true;
}

void tired_buffer_destroy(TiredBuffer *buffer)
{
    if (buffer == NULL)
        return;
    free(buffer->data);
    *buffer = (TiredBuffer){0};
}

bool tired_buffer_take(TiredBuffer *buffer, TiredText *output, TiredError *error)
{
    assert(buffer != NULL && output != NULL);
    if (buffer->data == NULL && !tired_buffer_append(buffer, NULL, 0, error))
        return false;
    tired_text_destroy(output);
    *output = (TiredText){buffer->data, buffer->length};
    *buffer = (TiredBuffer){0};
    tired_error_clear(error);
    return true;
}

typedef enum
{
    TOKEN,
    DIRECTIVE,
    DISPLAY
} Encoding;
static bool encode(const char *data, size_t length, Encoding context, TiredText *output,
                   TiredError *error)
{
    assert(output != NULL && (data != NULL || length == 0));
    if (length > TIRED_INPUT_LIMIT)
        return tired_error_set(error, TIRED_INVALID, "input-limit",
                               "Text exceeds the encoding input limit.", 0);
    if (!tired_validate_text(data, length, context == DIRECTIVE, error))
        return false;
    if (context == DIRECTIVE && length != 0 &&
        (data[0] == ' ' || data[length - 1] == ' ' || data[length - 1] == '\\'))
        return tired_error_set(
            error, TIRED_INVALID, "directive-text",
            "Scalar text has unsupported boundary whitespace or a trailing backslash.", 0);
    TiredBuffer result;
    tired_buffer_init(&result, 4U * TIRED_INPUT_LIMIT + 2U);
    if (context == TOKEN && !tired_buffer_append(&result, "\"", 1, error))
        goto fail;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)data[i];
        if (context != DIRECTIVE && (c < 32 || c == 127))
        {
            char escaped[] = {'\\', 'x', hex[c >> 4], hex[c & 15]};
            if (!tired_buffer_append(&result, escaped, sizeof(escaped), error))
                goto fail;
        }
        else if (context == DISPLAY && c == 0xc2 && i + 1 < length &&
                 (unsigned char)data[i + 1] >= 0x80 && (unsigned char)data[i + 1] <= 0x9f)
        {
            /* C1 controls may be interpreted by terminal emulators as controls. */
            unsigned char next = (unsigned char)data[++i];
            char escaped[] = {'\\', 'u', '0', '0', hex[next >> 4], hex[next & 15]};
            if (!tired_buffer_append(&result, escaped, sizeof(escaped), error))
                goto fail;
        }
        else
        {
            if (context != DISPLAY && c == '%')
            {
                if (!tired_buffer_append(&result, "%", 1, error))
                    goto fail;
            }
            if ((context == TOKEN && (c == '\\' || c == '"')) || (context == DISPLAY && c == '\\'))
            {
                if (!tired_buffer_append(&result, "\\", 1, error))
                    goto fail;
            }
            if (!tired_buffer_append(&result, (const char *)&data[i], 1, error))
                goto fail;
        }
    }
    if (context == TOKEN && !tired_buffer_append(&result, "\"", 1, error))
        goto fail;
    return tired_buffer_take(&result, output, error);
fail:
    tired_buffer_destroy(&result);
    return false;
}

bool tired_encode_token(const char *data, size_t length, TiredText *output, TiredError *error)
{
    return encode(data, length, TOKEN, output, error);
}
bool tired_encode_directive(const char *data, size_t length, TiredText *output, TiredError *error)
{
    return encode(data, length, DIRECTIVE, output, error);
}
bool tired_encode_display(const char *data, size_t length, TiredText *output, TiredError *error)
{
    return encode(data, length, DISPLAY, output, error);
}
