#ifndef TIRED_PROFILE_SNAPSHOT_H
#define TIRED_PROFILE_SNAPSHOT_H
#include "tired/catalog.h"
#include "tired/profile_merge.h"
typedef struct
{
    TiredProfile profile;
    TiredText source_path;
    char source_sha256[65];
    TiredProfileOrigin source_origin;
    bool explicit_selection;
    TiredRecommendationDisposition *decisions;
    size_t count;
} TiredProfileSnapshot;
/* Historical profile document plus source provenance and one decision per
 * recommendation. The source digest describes original file bytes, not reserialized
 * JSON. Loading validates structure but establishes no source trust and reapplies
 * no recommendations. Parsed output owns all storage; encode views may borrow.
 * Atomic outputs, <=1 MiB snapshot and existing profile bounds. */
bool tired_profile_snapshot_encode(const TiredProfileSnapshot *snapshot, TiredText *output,
                                   TiredError *error);
bool tired_profile_snapshot_parse(const char *data, size_t length, TiredProfileSnapshot *output,
                                  TiredError *error);
void tired_profile_snapshot_destroy(TiredProfileSnapshot *snapshot);
#endif
