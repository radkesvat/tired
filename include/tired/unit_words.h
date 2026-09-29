#ifndef TIRED_UNIT_WORDS_H
#define TIRED_UNIT_WORDS_H
#include "tired/value.h"
typedef struct
{
    TiredText value;   /* Owned decoded bytes, potentially sensitive/non-UTF-8. */
    size_t start, end; /* Half-open source byte span, including quotes/escapes. */
} TiredUnitWord;
typedef struct
{
    TiredUnitWord *words;
    size_t count;
} TiredUnitWords;
/* Decode a single logical unit value using quoting/C escapes. Caller handles
 * sections, assignments, continuations and comments first. Preserves source
 * spans for display redaction; never expands %, $, command prefixes or a shell.
 * <=4 MiB source, <=4096 words, <=1 MiB decoded bytes including terminators.
 * Reject malformed quotes/escapes, NUL and invalid Unicode escape code points.
 * Byte escapes may yield non-UTF-8. Atomic owned output; no executable semantics. */
bool tired_unit_words_parse(const char *data, size_t length, TiredUnitWords *output,
                            TiredError *error);
/* Clears decoded bytes before releasing them. */
void tired_unit_words_destroy(TiredUnitWords *words);
#endif
