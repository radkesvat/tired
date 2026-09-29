#ifndef TIRED_UNIT_REDACTION_H
#define TIRED_UNIT_REDACTION_H
#include "tired/environment.h"
#include "tired/redaction.h"
/* Display installed bytes without regenerating a unit. saved_argv/environment
 * are API-valid private snapshots; classified is NULL or 4096 flags. They may be
 * NULL when unavailable. Masks command flag values, saved classifications and
 * matching saved sensitive words/assignment values; Environment names use the
 * environment heuristic and saved sensitivity. Inline SetCredential values are
 * always masked. Adds a non-installable-view label when any bytes are masked.
 * No variable/specifier evaluation, shell execution or file access. This is not
 * a complete secret detector. Atomic output; input <=4 MiB, output <=8 MiB.
 * Result bytes are not terminal-escaped; display callers must escape controls.
 * Unknown/malformed syntax returns an error instead of unredacted fallback. */
bool tired_unit_redact(const char *data, size_t length, const TiredTextList *saved_argv,
                       const bool *classified, const TiredEnvironment *environment,
                       TiredText *output, TiredRedaction *redaction, TiredError *error);
#endif
