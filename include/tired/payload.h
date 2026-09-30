#ifndef TIRED_PAYLOAD_H
#define TIRED_PAYLOAD_H
#include "tired/value.h"
/* Resolve installed payload relative to the actual executable, with configured
 * build-tree fallback. No environment or caller-supplied path selects a helper. */
bool tired_payload_path(bool helper, TiredText *path, TiredError *error);
#endif
