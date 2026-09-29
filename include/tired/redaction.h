#ifndef TIRED_REDACTION_H
#define TIRED_REDACTION_H
#include "tired/model.h"
typedef struct
{
    bool sensitive, redacted;
} TiredRedaction;
/* Deep-copy an API-valid model for display, masking classified argv values unless
 * include_sensitive is true. classified is NULL or TIRED_ARGUMENT_LIMIT flags;
 * index zero never hides the executable. Origins/inheritance/empty states survive.
 * Heuristics recognize password/passwd/token/secret/api-key flag names. This is
 * not a complete secret detector. It does not redact arbitrary unit-file bytes,
 * environment values or other model fields. Atomic owned model and flags outputs.
 * Callers enforce private export authorization before include_sensitive=true. */
bool tired_spec_display(const TiredServiceSpec *source, const bool *classified,
                        bool include_sensitive, TiredServiceSpec *output, TiredRedaction *redaction,
                        TiredError *error);
#endif
