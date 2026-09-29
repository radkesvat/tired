#ifndef TIRED_UNIT_DOCUMENT_H
#define TIRED_UNIT_DOCUMENT_H
#include "tired/value.h"
typedef struct
{
    TiredText section, key, value;
    /* value_offsets[i] locates value.data[i] in the original file. A continuation
     * space maps to the replaced backslash. Monotonic positions may have gaps. */
    uint32_t *value_offsets;
} TiredUnitAssignment;
typedef struct
{
    TiredUnitAssignment *assignments;
    size_t count;
} TiredUnitDocument;
/* Read-only syntax mapping for display; no directives are applied or validated.
 * Retains repeated/reset assignments in order. Handles CR/LF, comments and odd
 * trailing-backslash continuation, including intervening comment lines. Trims
 * assignment boundary whitespace. <=4 MiB source, <=1 MiB logical line, <=4096
 * assignments, <=255-byte section/key. Malformed syntax fails atomically; it is
 * not silently ignored as the manager may do. Value bytes may be sensitive. */
bool tired_unit_document_parse(const char *data, size_t length, TiredUnitDocument *output,
                               TiredError *error);
void tired_unit_document_destroy(TiredUnitDocument *document);
#endif
