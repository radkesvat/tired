#ifndef TIRED_NAME_H
#define TIRED_NAME_H

#include "tired/value.h"

#define TIRED_AUTO_NAME_LIMIT 80U
#define TIRED_EXPLICIT_NAME_LIMIT 200U

typedef enum
{
    TIRED_NAME_EXECUTABLE,
    TIRED_NAME_SCRIPT,
    TIRED_NAME_MODULE,
    TIRED_NAME_WRAPPER
} TiredNameBasis;

/* Pure passive naming: never opens or executes a workload. argv includes argv[0].
 * Output is an owned basename without .service; unchanged on failure.
 * Wrapper basis requests a warning; it never changes the actual command. */
bool tired_name_suggest(const TiredTextList *argv, TiredText *name, TiredNameBasis *basis,
                        TiredError *error);
/* Explicit names accept an optional .service suffix, removed in the result.
 * A basename is 1–200 ASCII bytes, starts/ends alphanumeric, and contains only
 * alphanumeric, dot, underscore or hyphen. Adjacent dots are rejected. */
bool tired_name_explicit(const char *input, size_t length, TiredText *name, TiredError *error);
/* Build a candidate full unit name. ordinal 1 is unsuffixed; 2 produces -2.
 * Collision discovery and reservation belong to the manager/transaction layer. */
bool tired_name_candidate(const TiredText *base, uint64_t ordinal, TiredText *unit_name,
                          TiredError *error);

#endif
