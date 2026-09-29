#include "tired/review_snapshot.h"
#include "tired/json.h"
#include <assert.h>
#include <string.h>
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "review-snapshot",
                           "Invalid historical review snapshot.", 0);
}
bool tired_review_snapshot_validate(const TiredReviewSnapshot *review, TiredError *error)
{
    assert(review != NULL);
    if (strnlen(review->approved_sha256, 65) != 64 || review->argument_count == 0 ||
        review->argument_count > TIRED_ARGUMENT_LIMIT || review->sensitive_arguments[0])
        return invalid(error);
    for (size_t i = 0; i < 64; ++i)
        if (!((review->approved_sha256[i] >= '0' && review->approved_sha256[i] <= '9') ||
              (review->approved_sha256[i] >= 'a' && review->approved_sha256[i] <= 'f')))
            return invalid(error);
    for (size_t i = review->argument_count; i < TIRED_ARGUMENT_LIMIT; ++i)
        if (review->sensitive_arguments[i])
            return invalid(error);
    tired_error_clear(error);
    return true;
}
static bool add(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
bool tired_review_snapshot_encode(const TiredReviewSnapshot *review, TiredText *output,
                                  TiredError *error)
{
    assert(review != NULL && output != NULL);
    if (!tired_review_snapshot_validate(review, error))
        return false;
    struct json_object *document = json_object_new_object(), *risks = json_object_new_array(),
                       *indices = json_object_new_array();
    bool ok = false;
    if (document == NULL || risks == NULL || indices == NULL)
        goto allocation;
    for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
        if (review->acknowledged[i])
        {
            struct json_object *value =
                json_object_new_string(tired_risk_get((TiredRiskId)i)->code);
            if (value == NULL || json_object_array_add(risks, value) != 0)
            {
                json_object_put(value);
                goto allocation;
            }
        }
    for (size_t i = 1; i < review->argument_count; ++i)
        if (review->sensitive_arguments[i])
        {
            struct json_object *value = json_object_new_uint64(i);
            if (value == NULL || json_object_array_add(indices, value) != 0)
            {
                json_object_put(value);
                goto allocation;
            }
        }
    bool inserted = add(document, "acknowledged_risks", risks);
    risks = NULL;
    if (!inserted)
        goto allocation;
    inserted = add(document, "sensitive_arguments", indices);
    indices = NULL;
    if (!inserted || !add(document, "schema_version", json_object_new_int(1)) ||
        !add(document, "argument_count", json_object_new_uint64(review->argument_count)) ||
        !add(document, "approved_sha256", json_object_new_string(review->approved_sha256)))
        goto allocation;
    const char *bytes = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        goto allocation;
    ok = tired_text_set(output, bytes, strlen(bytes), TIRED_INPUT_LIMIT, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode review snapshot.", 0);
done:
    json_object_put(indices);
    json_object_put(risks);
    json_object_put(document);
    return ok;
}
bool tired_review_snapshot_parse(const char *data, size_t length, TiredReviewSnapshot *output,
                                 TiredError *error)
{
    assert(output != NULL);
    TiredReviewSnapshot review = {0};
    struct json_object *document = NULL, *version = NULL, *count = NULL, *digest = NULL,
                       *risks = NULL, *indices = NULL;
    uint64_t schema, argc;
    bool ok = false;
    if (!tired_json_parse(data, length, TIRED_INPUT_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) != 5 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "argument_count", &count) ||
        !tired_json_u64(count, 1, TIRED_ARGUMENT_LIMIT, &argc, error) ||
        !json_object_object_get_ex(document, "approved_sha256", &digest) ||
        !json_object_is_type(digest, json_type_string) ||
        json_object_get_string_len(digest) != 64 ||
        !json_object_object_get_ex(document, "acknowledged_risks", &risks) ||
        !json_object_is_type(risks, json_type_array) ||
        json_object_array_length(risks) > TIRED_RISK_COUNT ||
        !json_object_object_get_ex(document, "sensitive_arguments", &indices) ||
        !json_object_is_type(indices, json_type_array) || json_object_array_length(indices) >= argc)
        goto bad;
    review.argument_count = (size_t)argc;
    memcpy(review.approved_sha256, json_object_get_string(digest), 65);
    for (size_t i = 0; i < json_object_array_length(risks); ++i)
    {
        struct json_object *value = json_object_array_get_idx(risks, i);
        TiredRiskId id;
        if (!json_object_is_type(value, json_type_string) ||
            !tired_risk_find(json_object_get_string(value),
                             (size_t)json_object_get_string_len(value), &id) ||
            review.acknowledged[id])
            goto bad;
        review.acknowledged[id] = true;
    }
    for (size_t i = 0; i < json_object_array_length(indices); ++i)
    {
        uint64_t index;
        if (!tired_json_u64(json_object_array_get_idx(indices, i), 1, argc - 1, &index, error) ||
            review.sensitive_arguments[index])
            goto bad;
        review.sensitive_arguments[index] = true;
    }
    if (!tired_review_snapshot_validate(&review, error))
        goto done;
    *output = review;
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    json_object_put(document);
    return ok;
}
