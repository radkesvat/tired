#ifndef TIRED_SHOW_EFFECTIVE_H
#define TIRED_SHOW_EFFECTIVE_H
#include "tired/effective_files.h"
#include "tired/encode.h"
#include "tired/unit_batch.h"
/* CLI renderers borrow validated observations. JSON returns a new owned object;
 * text appends to the caller's bounded buffer. File displays must already have
 * their redaction/export policy applied. Neither function performs I/O. */
struct json_object *tired_show_effective_json(const TiredEffectiveFiles *files,
                                              const TiredUnitBatchItem *live, bool include_text);
bool tired_show_effective_text(const TiredEffectiveFiles *files, bool terminal, TiredBuffer *buffer,
                               TiredError *error);
#endif
