#ifndef TIRED_UI_DIFF_H
#define TIRED_UI_DIFF_H
#include "tired/service_record.h"
/* Stable line-aligned unified display with three context lines. Inputs must
 * already be redacted. This is a display artifact, not an installable patch;
 * terminal callers still escape controls. Bounded work rejects oversized
 * comparisons instead of producing a misleading lockstep approximation. */
bool tired_ui_unified_diff(const char *before_name, const char *after_name, const TiredText *before,
                           const TiredText *after, TiredText *output, TiredError *error);
/* Typed saved-to-proposed values and origins, including model-only actions.
 * Secret argv and environment values stay masked on both sides. Changed masked
 * values remain visible as a change even when both displays are identical. */
bool tired_ui_field_diff(const TiredServiceRecord *before, const TiredServiceRecord *after,
                         TiredText *output, TiredError *error);
#endif
