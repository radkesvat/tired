#ifndef TIRED_SETTINGS_H
#define TIRED_SETTINGS_H
#include "tired/value.h"
#include <sys/types.h>

typedef enum
{
    TIRED_SETTING_COLOR,
    TIRED_SETTING_ASCII,
    TIRED_SETTING_TUI,
    TIRED_SETTING_RETRY,
    TIRED_SETTING_RESTART_DELAY,
    TIRED_SETTING_HISTORY,
    TIRED_SETTING_LOG_TAIL,
    TIRED_SETTING_OBSERVATION,
    TIRED_SETTING_PROFILE_DIRECTORIES,
    TIRED_SETTING_COUNT
} TiredSettingId;
typedef enum
{
    TIRED_SETTINGS_DEFAULT,
    TIRED_SETTINGS_ADMIN,
    TIRED_SETTINGS_USER,
    TIRED_SETTINGS_CLI
} TiredSettingsOrigin;
typedef struct
{
    unsigned color; /* auto, always, never */
    bool ascii, tui, limited_retries;
    uint64_t restart_usec, history_revisions, log_tail, observation_usec;
    TiredTextList profile_directories;
    bool supplied[TIRED_SETTING_COUNT];
    TiredSettingsOrigin origins[TIRED_SETTING_COUNT];
} TiredSettings;

/* Initialize owning destinations to zero before first use. Defaults replaces any
 * existing owned data. Destroy accepts NULL. */
void tired_settings_defaults(TiredSettings *settings);
void tired_settings_destroy(TiredSettings *settings);
/* Parse partial settings with defaults plus explicit-field markers. No I/O/trust
 * decision. Reject unknown keys and schemas; failure preserves output. Cross-field
 * constraints involving omitted fields are checked when merging into defaults or
 * an effective configuration. Parsed layers must be merged before use. */
bool tired_settings_parse(const char *data, size_t length, TiredSettings *settings,
                          TiredError *error);
/* Apply explicit fields atomically. User settings cannot add administrative profile
 * locations. No setting can disable validation or alter privileged state roots. */
bool tired_settings_merge(TiredSettings *settings, const TiredSettings *input,
                          TiredSettingsOrigin origin, TiredError *error);
/* Load optional absolute settings paths over defaults, administrator then user.
 * NULL skips a layer; ENOENT means absent. Other errors preserve the destination.
 * Paths must have normalized components, no symlinks, and ancestors owned by root
 * or the layer's owner with no group/other write bits. Files must be regular with
 * one link and the same permission rule. Administrator owner is always root.
 * This API does not select paths or read environment variables. */
bool tired_settings_load(const char *administrator_path, const char *user_path, uid_t user,
                         TiredSettings *settings, TiredError *error);
/* Format all effective fields and origins. No filesystem or manager access. */
bool tired_settings_output(const TiredSettings *settings, bool json, TiredText *output,
                           TiredError *error);
#endif
