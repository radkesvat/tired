#ifndef TIRED_SERVICE_RECORD_H
#define TIRED_SERVICE_RECORD_H
#include "tired/environment_snapshot.h"
#include "tired/executable_evidence.h"
#include "tired/layout.h"
#include "tired/model_format.h"
#include "tired/profile_snapshot.h"
#include "tired/review_snapshot.h"
#include "tired/service_metadata.h"
#define TIRED_SERVICE_RECORD_LIMIT (16U * TIRED_INPUT_LIMIT)
typedef struct
{
    TiredServiceMetadata metadata;
    TiredServiceSpec spec;
    TiredEnvironment environment;
    TiredCredentials credentials;
    TiredExecutableEvidence executable;
    TiredReviewSnapshot review;
    bool has_profile, has_environment, environment_loaded;
    bool linger_requested;
    TiredProfileSnapshot profile;
    TiredText unit_path, environment_path;
    char environment_revision[37], environment_sha256[65];
    TiredTextList external_config_paths;
    TiredTextList former_unit_names;
} TiredServiceRecord;
/* Complete private record composition. Command data is unredacted; environment
 * values are omitted and hydrated from their immutable private revision.
 * Structural parsing binds related fields but proves no installation, approval,
 * current ownership or live state. Paths are evidence, never mutation destinations.
 * Owned outputs start zeroed and are atomic on failure. Encode API-valid components. */
bool tired_service_record_encode(const TiredServiceRecord *record, TiredText *output,
                                 TiredError *error);
bool tired_service_record_parse(const char *data, size_t length, TiredServiceRecord *output,
                                TiredError *error);
/* Compare recorded paths with destinations derived from the current trusted layout.
 * No filesystem access. A successful result is still not write authorization. */
bool tired_service_record_check_layout(const TiredServiceRecord *record, const TiredLayout *layout,
                                       TiredError *error);
void tired_service_record_destroy(TiredServiceRecord *record);
/* Load exact values from the derived private immutable revision, preserving
 * names/origins/classifications from metadata and verifying its digest. */
bool tired_service_record_hydrate(TiredServiceRecord *record, const TiredLayout *layout,
                                  TiredError *error);
#endif
