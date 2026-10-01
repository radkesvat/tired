#ifndef TIRED_PAYLOAD_H
#define TIRED_PAYLOAD_H
#include "tired/value.h"
/* DIRECT profile paths use the builtin: catalogue. Other payload is resolved
 * relative to the executable, with configured build-tree fallback. */
bool tired_payload_path(bool helper, TiredText *path, TiredError *error);
typedef bool (*TiredHelperPreparer)(bool interactive, TiredText *path, TiredError *error);
/* The direct frontend registers its bundled helper provider at startup. Helpers
 * and distribution builds retain installed-only lookup. No environment override. */
void tired_payload_set_helper_preparer(TiredHelperPreparer prepare);
bool tired_payload_prepare_helper(bool interactive, TiredText *path, TiredError *error);
#endif
