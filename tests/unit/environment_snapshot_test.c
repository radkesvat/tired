#include "tired/environment_snapshot.h"
#include "tired/json.h"
#include <stdio.h>
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
int main(void)
{
    int result = 1;
    TiredEnvironment source = {0}, parsed = {0};
    TiredCredentials credentials = {0}, refs = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *document = NULL, *array = NULL, *entry = NULL;
    CHECK(tired_environment_snapshot_encode(&source, &credentials, &encoded, &error));
    CHECK(tired_environment_snapshot_parse(encoded.data, encoded.length, &parsed, &refs, &error));
    CHECK(parsed.count == 0 && refs.count == 0);
    CHECK(tired_environment_set(&source, "TOKEN=abc\nxyz", 13, TIRED_ENV_EXPLICIT, false, &error));
    CHECK(tired_environment_set(&source, "EMPTY=", 6, TIRED_ENV_IMPORTED, false, &error));
    CHECK(tired_environment_set(&source, "PLAIN=a=b c", 11, TIRED_ENV_PROFILE, false, &error));
    CHECK(tired_credentials_add(&credentials, "key=/not/read/by/snapshot", 25, &error));
    CHECK(tired_environment_snapshot_encode(&source, &credentials, &encoded, &error));
    CHECK(tired_environment_snapshot_parse(encoded.data, encoded.length, &parsed, &refs, &error));
    CHECK(parsed.count == 3 && refs.count == 1);
    const TiredEnvironmentEntry *token = tired_environment_find(&parsed, "TOKEN", 5);
    CHECK(token != NULL && token->sensitive && token->origin == TIRED_ENV_EXPLICIT &&
          strcmp(token->value.data, "abc\nxyz") == 0);
    CHECK(tired_environment_find(&parsed, "EMPTY", 5)->value.length == 0);
    CHECK(strcmp(refs.items[0].path.data, "/not/read/by/snapshot") == 0);
    CHECK(tired_environment_snapshot_encode(&parsed, &refs, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_ENVIRONMENT_SNAPSHOT_LIMIT,
                           &document, &error));
    CHECK(json_object_object_get_ex(document, "environment", &array));
    entry = json_object_array_get_idx(array, 0);
    CHECK(json_object_object_add(entry, "sensitive", json_object_new_boolean(false)) == 0);
    const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_environment_snapshot_parse(bad, strlen(bad), &parsed, &refs, &error));
    CHECK(parsed.count == 3 && refs.count == 1 &&
          tired_environment_find(&parsed, "TOKEN", 5)->sensitive);
    CHECK(json_object_object_add(entry, "sensitive", json_object_new_boolean(true)) == 0);
    CHECK(json_object_array_add(array, json_object_get(entry)) == 0);
    bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_environment_snapshot_parse(bad, strlen(bad), &parsed, &refs, &error));
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_ENVIRONMENT_SNAPSHOT_LIMIT,
                           &document, &error));
    CHECK(json_object_object_get_ex(document, "credentials", &array));
    entry = json_object_array_get_idx(array, 0);
    CHECK(json_object_object_add(entry, "name", json_object_new_string("key=/elsewhere")) == 0);
    bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_environment_snapshot_parse(bad, strlen(bad), &parsed, &refs, &error));
    CHECK(strcmp(refs.items[0].name.data, "key") == 0);
    result = 0;
cleanup:
    json_object_put(document);
    tired_environment_destroy(&source);
    tired_environment_destroy(&parsed);
    tired_credentials_destroy(&credentials);
    tired_credentials_destroy(&refs);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
