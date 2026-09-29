#ifndef TIRED_SETTINGS_H
#define TIRED_SETTINGS_H
#include "tired/value.h"

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
    TIRED_SETTINGS_USER
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
#endif
