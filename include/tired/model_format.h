#ifndef TIRED_MODEL_FORMAT_H
#define TIRED_MODEL_FORMAT_H
#include "tired/model.h"
/* Private schema-1 model snapshot. Every registry field records origin, explicit
 * inheritance and value (canonical scalar string, ordered string array or null).
 * Encode API-validated models only. Includes unredacted argv/text: never use as public diagnostics.
 * Strict bounded parsing through model setters; no defaults are recomputed. Atomic owned output.
 * This stores ServiceSpec only; environment/identity/profile evidence are separate. */
bool tired_spec_encode(const TiredServiceSpec *spec, TiredText *output, TiredError *error);
bool tired_spec_parse(const char *data, size_t length, TiredServiceSpec *output, TiredError *error);
#endif
