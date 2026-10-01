#ifndef TIRED_PROFILE_H
#define TIRED_PROFILE_H
#include "tired/json.h"
#include "tired/model.h"

#define TIRED_PROFILE_LIMIT (256U * 1024U)
typedef struct
{
    TiredFieldId field;
    TiredFieldValue value;
    unsigned scopes; /* bit 0 system, bit 1 user */
    bool automatic;
    const char *reason, *strength, *risk; /* Borrowed from profile document. */
    struct json_object *conditions;
} TiredRecommendation;
typedef struct
{
    struct json_object *document;
    const char *id, *name, *summary;
    uint64_t revision, systemd_min;
    bool case_sensitive, version_restricted;
    bool default_root; /* Creation-time system-scope account default; not authorization. */
    TiredTextList basenames;
    TiredRecommendation *recommendations;
    size_t count;
} TiredProfile;
/* Parses/validates a profile only; does not establish file trust or apply advice.
 * All owned outputs start zeroed and are preserved on failure. */
bool tired_profile_parse(const char *data, size_t length, TiredProfile *profile, TiredError *error);
void tired_profile_destroy(TiredProfile *profile);
bool tired_profile_matches(const TiredProfile *profile, const TiredText *executable);
#endif
