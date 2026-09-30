#ifndef TIRED_LAYOUT_H
#define TIRED_LAYOUT_H
#include "tired/value.h"
typedef enum
{
    TIRED_PATH_CONFIG,
    TIRED_PATH_PROFILES,
    TIRED_PATH_ENVIRONMENT_SERVICES,
    TIRED_PATH_UNITS,
    TIRED_PATH_RECORDS,
    TIRED_PATH_HISTORY,
    TIRED_PATH_TRANSACTIONS,
    TIRED_PATH_OPERATION_LOCK,
    TIRED_PATH_COUNT
} TiredLayoutPath;
typedef struct
{
    bool user_scope;
    TiredText paths[TIRED_PATH_COUNT];
} TiredLayout;
/* Pure path derivation. System scope ignores all supplied user paths. For user
 * scope, home is the account database home, config/state are optional XDG values
 * (NULL/empty use home defaults), and runtime is mandatory. Absolute normalized
 * paths only; trailing separators are removed. No filesystem access or creation,
 * and no ownership/accessibility/write authorization is inferred. Atomic output. */
bool tired_layout_resolve(bool user_scope, const char *home, const char *config, const char *state,
                          const char *runtime, TiredLayout *layout, TiredError *error);
/* Production discovery: system paths never inspect HOME or XDG. User paths use
 * current account home and XDG values, requiring matched real/effective IDs. */
bool tired_layout_discover(bool user_scope, TiredLayout *layout, TiredError *error);
/* Configuration/profile paths only; usable without a session or user manager.
 * Other paths remain unset. Never creates or guesses a runtime directory. */
bool tired_layout_discover_profiles(bool user_scope, TiredLayout *layout, TiredError *error);
/* User destinations must occur in the selected manager's complete UnitPath list.
 * Comparison is lexical (trailing separators ignored), without filesystem alias
 * resolution. System scope has no user XDG check. No missing-path fallback. */
bool tired_layout_check_unit_path(const TiredLayout *layout, const TiredTextList *load_paths,
                                  TiredError *error);
void tired_layout_destroy(TiredLayout *layout);
#endif
