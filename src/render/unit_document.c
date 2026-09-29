#include "tired/unit_document.h"
#include "tired/capture.h"
#include "tired/encode.h"
#include <assert.h>
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>

static void assignment_destroy(TiredUnitAssignment *assignment)
{
    tired_text_destroy(&assignment->section);
    tired_text_destroy(&assignment->key);
    if (assignment->value.data != NULL)
        OPENSSL_cleanse(assignment->value.data, assignment->value.length);
    tired_text_destroy(&assignment->value);
    free(assignment->value_offsets);
    *assignment = (TiredUnitAssignment){0};
}
void tired_unit_document_destroy(TiredUnitDocument *document)
{
    if (document == NULL)
        return;
    for (size_t i = 0; i < document->count; ++i)
        assignment_destroy(&document->assignments[i]);
    free(document->assignments);
    *document = (TiredUnitDocument){0};
}
static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "unit-document-syntax",
                           "Unit text contains malformed or unsupported assignment syntax.", 0);
}
static bool parse_line(const TiredBuffer *line, const uint32_t *offsets, TiredText *section,
                       TiredUnitDocument *document, size_t *capacity, TiredError *error)
{
    size_t start = 0, end = line->length;
    while (start < end && space(line->data[start]))
        ++start;
    while (end > start && space(line->data[end - 1]))
        --end;
    if (start == end)
        return true;
    if (!tired_validate_text(line->data + start, end - start, false, error))
        return false;
    if (line->data[start] == '[')
    {
        if (line->data[end - 1] != ']' || end - start < 3)
            return invalid(error);
        return tired_text_set(section, line->data + start + 1, end - start - 2, 255, error);
    }
    const char *equal = memchr(line->data + start, '=', end - start);
    if (equal == NULL || section->data == NULL)
        return invalid(error);
    size_t key_end = (size_t)(equal - line->data), value_start = key_end + 1;
    while (key_end > start && space(line->data[key_end - 1]))
        --key_end;
    while (value_start < end && space(line->data[value_start]))
        ++value_start;
    if (key_end == start || document->count == 4096)
        return invalid(error);
    TiredUnitAssignment assignment = {0};
    bool ok = false;
    if (!tired_text_set(&assignment.section, section->data, section->length, 255, error) ||
        !tired_text_set(&assignment.key, line->data + start, key_end - start, 255, error) ||
        !tired_text_set(&assignment.value, line->data + value_start, end - value_start,
                        TIRED_INPUT_LIMIT, error))
        goto done;
    if (assignment.value.length != 0)
    {
        assignment.value_offsets = malloc(assignment.value.length * sizeof(*offsets));
        if (assignment.value_offsets == NULL)
            goto allocation;
        memcpy(assignment.value_offsets, offsets + value_start,
               assignment.value.length * sizeof(*offsets));
    }
    if (document->count == *capacity)
    {
        size_t next = *capacity == 0 ? 16 : *capacity * 2;
        TiredUnitAssignment *grown = realloc(document->assignments, next * sizeof(*grown));
        if (grown == NULL)
            goto allocation;
        document->assignments = grown;
        *capacity = next;
    }
    document->assignments[document->count++] = assignment;
    assignment = (TiredUnitAssignment){0};
    ok = true;
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate unit syntax mapping.", 0);
done:
    assignment_destroy(&assignment);
    return ok;
}
bool tired_unit_document_parse(const char *data, size_t length, TiredUnitDocument *output,
                               TiredError *error)
{
    assert((data != NULL || length == 0) && output != NULL);
    if (length > TIRED_UNIT_LIMIT || (length != 0 && memchr(data, '\0', length) != NULL))
        return invalid(error);
    TiredUnitDocument document = {0};
    TiredText section = {0};
    TiredBuffer line;
    tired_buffer_init(&line, TIRED_INPUT_LIMIT);
    uint32_t *offsets = length == 0 ? NULL : malloc(length * sizeof(*offsets));
    bool ok = false, continued = false, bom_seen = false;
    size_t offset = 0, capacity = 0;
    if (length != 0 && offsets == NULL)
    {
        tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate unit source offsets.",
                        0);
        goto done;
    }
    while (offset < length)
    {
        size_t start = offset;
        while (offset < length && data[offset] != '\n' && data[offset] != '\r')
            ++offset;
        size_t end = offset;
        if (offset < length)
        {
            char delimiter = data[offset++];
            if (offset < length && (data[offset] == '\r' || data[offset] == '\n') &&
                data[offset] != delimiter)
                ++offset;
        }
        size_t first = start;
        while (first < end && space(data[first]))
            ++first;
        if (first < end && (data[first] == '#' || data[first] == ';'))
            continue;
        if (!bom_seen && end - start >= 3 && memcmp(data + start, "\xef\xbb\xbf", 3) == 0)
        {
            start += 3;
            bom_seen = true;
        }
        size_t old = line.length;
        if (!tired_buffer_append(&line, data + start, end - start, error))
            goto done;
        for (size_t i = start; i < end; ++i)
            offsets[old + i - start] = (uint32_t)i;
        size_t backslashes = 0;
        while (backslashes < line.length && line.data[line.length - backslashes - 1] == '\\')
            ++backslashes;
        continued = (backslashes % 2) != 0;
        if (continued)
        {
            line.data[line.length - 1] = ' ';
            continue;
        }
        if (!parse_line(&line, offsets, &section, &document, &capacity, error))
            goto done;
        if (line.data != NULL)
            OPENSSL_cleanse(line.data, line.length);
        line.length = 0;
    }
    if (continued && !parse_line(&line, offsets, &section, &document, &capacity, error))
        goto done;
    tired_unit_document_destroy(output);
    *output = document;
    document = (TiredUnitDocument){0};
    tired_error_clear(error);
    ok = true;
done:
    if (line.data != NULL)
        OPENSSL_cleanse(line.data, line.length);
    tired_buffer_destroy(&line);
    free(offsets);
    tired_text_destroy(&section);
    tired_unit_document_destroy(&document);
    return ok;
}
