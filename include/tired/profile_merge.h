#ifndef TIRED_PROFILE_MERGE_H
#define TIRED_PROFILE_MERGE_H
#include "tired/profile.h"

typedef enum
{
    TIRED_FACT_UNKNOWN,
    TIRED_FACT_FALSE,
    TIRED_FACT_TRUE
} TiredFact;
typedef struct
{
    uint64_t systemd_version; /* 0 means unknown. Offline callers may declare a target baseline. */
    const char *application_version; /* NULL means unknown, never obtained by probing. */
    TiredFact features[5]; /* credentials, memory-max, cpu-quota, tasks-max, ambient-capabilities */
    TiredFact inspections[2]; /* executable-regular, working-directory-accessible */
    bool nofile_known;
    TiredLimit nofile_ceiling; /* Proven usable ceiling after host/manager/identity checks. */
} TiredProfileContext;
typedef enum
{
    TIRED_RECOMMENDATION_APPLIED,
    TIRED_RECOMMENDATION_SCOPE,
    TIRED_RECOMMENDATION_CONDITION_FALSE,
    TIRED_RECOMMENDATION_CONDITION_UNKNOWN,
    TIRED_RECOMMENDATION_VERSION_UNKNOWN,
    TIRED_RECOMMENDATION_INCOMPATIBLE,
    TIRED_RECOMMENDATION_SUGGESTION,
    TIRED_RECOMMENDATION_USER_OVERRIDE,
    TIRED_RECOMMENDATION_CONFLICT,
    TIRED_RECOMMENDATION_REQUIRED_CONFLICT
} TiredRecommendationDisposition;
typedef struct
{
    TiredServiceSpec spec;
    TiredRecommendationDisposition *decisions; /* One per profile recommendation, same order. */
    size_t count;
} TiredProfileMerge;
const char *tired_recommendation_disposition_name(TiredRecommendationDisposition disposition);
/* Type is resolved first; remaining predicates see the selected final type.
 * Conflicting automatic recommendations retain the input field. Never mutates
 * input or profile. Does not certify file trust, host ceilings, or risk approval. */
bool tired_profile_merge(const TiredProfile *profile, const TiredServiceSpec *input,
                         const TiredProfileContext *context, TiredProfileMerge *output,
                         TiredError *error);
void tired_profile_merge_destroy(TiredProfileMerge *merge);
#endif
