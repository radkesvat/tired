#ifndef TIRED_PLAN_H
#define TIRED_PLAN_H
#include "tired/catalog.h"
#include "tired/cli.h"
#include "tired/profile_merge.h"
#include "tired/proposal.h"

typedef struct
{
    TiredInvocation invocation;
    TiredAccount invoking, service;
    TiredGroup group;
    TiredServiceSpec spec;
    TiredEnvironment environment;
    TiredCredentials credentials;
    TiredText managed_environment;
    TiredNameBasis name_basis;
    char uuid[37];
    TiredProfile profile;
    TiredText profile_path;
    char profile_digest[65];
    TiredProfileOrigin profile_origin;
    TiredTextList profile_candidates;
    TiredRecommendationDisposition *profile_decisions;
    bool profile_matching;
    bool profile_explicit;
} TiredPlan;
/* Read-only preparation. Reads only explicit inputs/account database and captures
 * PATH once. Produces no files or manager changes. Profile selection is supplied
 * by the profile layer; this constructor builds a generic proposal plus overrides. */
bool tired_plan_prepare(const TiredRequest *request, TiredPlan *output, TiredError *error);
void tired_plan_destroy(TiredPlan *plan);
/* Apply a selected catalog snapshot atomically and retain provenance. */
bool tired_plan_apply_profiles(TiredPlan *plan, const TiredProfileCatalog *catalog,
                               const char *selection, const TiredProfileContext *context,
                               TiredError *error);
#endif
