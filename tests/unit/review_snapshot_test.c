#include "tired/json.h"
#include "tired/review_snapshot.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(void)
{
    int result = 1;
    TiredReviewSnapshot source =
                            {.approved_sha256 =
                                 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                             .argument_count = 4},
                        parsed = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *object = NULL, *array = NULL;
    source.acknowledged[TIRED_RISK_ROOT] = true;
    source.acknowledged[TIRED_RISK_SENSITIVE_COMMAND] = true;
    source.sensitive_arguments[2] = source.sensitive_arguments[3] = true;
    CHECK(tired_review_snapshot_encode(&source, &encoded, &error));
    CHECK(tired_review_snapshot_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.argument_count == 4 && parsed.acknowledged[TIRED_RISK_ROOT] &&
          parsed.acknowledged[TIRED_RISK_SENSITIVE_COMMAND]);
    CHECK(!parsed.acknowledged[TIRED_RISK_CAPABILITIES] && parsed.sensitive_arguments[2] &&
          parsed.sensitive_arguments[3] && !parsed.sensitive_arguments[1]);
    CHECK(tired_review_snapshot_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    source.sensitive_arguments[0] = true;
    CHECK(!tired_review_snapshot_encode(&source, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    source.sensitive_arguments[0] = false;
    source.sensitive_arguments[4] = true;
    CHECK(!tired_review_snapshot_validate(&source, &error));
    source.sensitive_arguments[4] = false;
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &object, &error));
    CHECK(json_object_object_get_ex(object, "sensitive_arguments", &array));
    CHECK(json_object_array_put_idx(array, 1, json_object_new_int(2)) == 0);
    const char *bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_review_snapshot_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.sensitive_arguments[3] && parsed.argument_count == 4);
    CHECK(json_object_array_put_idx(array, 1, json_object_new_int(4)) == 0);
    bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_review_snapshot_parse(bad, strlen(bad), &parsed, &error));
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &object, &error));
    CHECK(json_object_object_get_ex(object, "acknowledged_risks", &array));
    CHECK(json_object_array_add(
              array, json_object_new_string(tired_risk_get(TIRED_RISK_ROOT)->code)) == 0);
    bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_review_snapshot_parse(bad, strlen(bad), &parsed, &error));
    CHECK(json_object_array_put_idx(array, 2, json_object_new_string("unknown-risk")) == 0);
    bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_review_snapshot_parse(bad, strlen(bad), &parsed, &error));
    source.argument_count = TIRED_ARGUMENT_LIMIT;
    source.sensitive_arguments[TIRED_ARGUMENT_LIMIT - 1] = true;
    CHECK(tired_review_snapshot_encode(&source, &encoded, &error));
    CHECK(tired_review_snapshot_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.sensitive_arguments[TIRED_ARGUMENT_LIMIT - 1]);
    result = 0;
cleanup:
    json_object_put(object);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
