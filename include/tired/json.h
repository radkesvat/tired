#ifndef TIRED_JSON_H
#define TIRED_JSON_H
#include "tired/value.h"
#include <json-c/json.h>

/* Bounded strict JSON, including decoded duplicate-key detection, integer range,
 * Unicode scalar validation, and rejection of NUL. No comments/NaN/trailing data.
 * Caller owns the returned reference, including successful JSON null (NULL).
 * *output must start NULL or own a reference; failure leaves it unchanged. */
bool tired_json_parse(const char *data, size_t length, size_t byte_limit,
                      struct json_object **output, TiredError *error);
bool tired_json_u64(struct json_object *value, uint64_t minimum, uint64_t maximum, uint64_t *output,
                    TiredError *error);
#endif
