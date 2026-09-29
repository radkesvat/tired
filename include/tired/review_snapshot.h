#ifndef TIRED_REVIEW_SNAPSHOT_H
#define TIRED_REVIEW_SNAPSHOT_H
#include "tired/risk.h"
typedef struct
{
    char approved_sha256[65];
    size_t argument_count;
    bool acknowledged[TIRED_RISK_COUNT];
    bool sensitive_arguments[TIRED_ARGUMENT_LIMIT];
} TiredReviewSnapshot;
/* Historical review evidence, not authorization. Digest must be checked against
 * the complete approved request; caller reruns current risk/precondition checks.
 * Argument zero cannot be marked; indices must be unique and within argument_count.
 * Risk codes are known, unique identifiers. No unredacted argument values stored.
 * Atomic fixed-size parse output. */
bool tired_review_snapshot_validate(const TiredReviewSnapshot *review, TiredError *error);
bool tired_review_snapshot_encode(const TiredReviewSnapshot *review, TiredText *output,
                                  TiredError *error);
bool tired_review_snapshot_parse(const char *data, size_t length, TiredReviewSnapshot *review,
                                 TiredError *error);
#endif
