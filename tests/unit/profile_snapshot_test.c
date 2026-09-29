#include "tired/io.h"
#include "tired/profile_snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    int result = 1;
    TiredProfileSnapshot
        source = {.source_origin = TIRED_PROFILE_BUNDLED,
                  .source_sha256 =
                      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                  .explicit_selection = true},
        parsed = {0};
    TiredText bytes = {0}, encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *document = NULL, *decisions = NULL;
    CHECK(argc == 2 && tired_read_file(argv[1], TIRED_PROFILE_LIMIT, &bytes, &error));
    CHECK(tired_profile_parse(bytes.data, bytes.length, &source.profile, &error));
    CHECK(tired_text_set(&source.source_path, argv[1], strlen(argv[1]), 4096, &error));
    source.count = source.profile.count;
    CHECK(source.count > 0);
    source.decisions = calloc(source.count, sizeof(*source.decisions));
    CHECK(source.decisions != NULL);
    for (size_t i = 0; i < source.count; ++i)
        source.decisions[i] =
            i == 0 ? TIRED_RECOMMENDATION_APPLIED : TIRED_RECOMMENDATION_USER_OVERRIDE;
    CHECK(tired_profile_snapshot_encode(&source, &encoded, &error));
    CHECK(tired_profile_snapshot_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.count == source.count && strcmp(parsed.profile.id, source.profile.id) == 0 &&
          parsed.profile.revision == source.profile.revision && parsed.explicit_selection);
    CHECK(parsed.source_origin == TIRED_PROFILE_BUNDLED &&
          strcmp(parsed.source_sha256, source.source_sha256) == 0);
    for (size_t i = 0; i < source.count; ++i)
        CHECK(parsed.decisions[i] == source.decisions[i]);
    CHECK(tired_profile_snapshot_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    --source.count;
    CHECK(!tired_profile_snapshot_encode(&source, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    ++source.count;
    source.source_sha256[0] = 'A';
    CHECK(!tired_profile_snapshot_encode(&source, &again, &error));
    source.source_sha256[0] = '0';
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "decisions", &decisions));
    CHECK(json_object_array_put_idx(decisions, 0, json_object_new_string("invented")) == 0);
    const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_snapshot_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.decisions[0] == TIRED_RECOMMENDATION_APPLIED);
    CHECK(json_object_array_put_idx(decisions, 0, json_object_new_string("applied")) == 0);
    CHECK(json_object_array_add(decisions, json_object_new_string("applied")) == 0);
    bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_profile_snapshot_parse(bad, strlen(bad), &parsed, &error));
    result = 0;
cleanup:
    json_object_put(document);
    tired_profile_snapshot_destroy(&source);
    tired_profile_snapshot_destroy(&parsed);
    tired_text_destroy(&bytes);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
