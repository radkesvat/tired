#ifndef TIRED_STAGE_H
#define TIRED_STAGE_H
#include "tired/value.h"

typedef struct TiredStage TiredStage;
/* Create a private temporary verification directory under root-owned /tmp.
 * base is already normalized, without an added .service suffix. No overwrite.
 * *stage must be NULL. On failure a non-NULL handle may remain for cleanup and
 * diagnosis; always remove and destroy any returned handle. */
bool tired_stage_create(const TiredText *base, const char *unit, size_t length, TiredStage **stage,
                        TiredError *error);
const char *tired_stage_unit_path(const TiredStage *stage);
const char *tired_stage_directory(const TiredStage *stage);
/* Removes only the recorded file/directory identities. Conflicts or unexpected
 * contents fail explicitly and preserve replacements. Safe to retry. */
bool tired_stage_remove(TiredStage *stage, TiredError *error);
/* Releases memory/descriptors only. Does not hide cleanup failures by removing
 * anything. Call remove first; if it fails report the retained directory. */
void tired_stage_destroy(TiredStage *stage);
#endif
