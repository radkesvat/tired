#include "tired/file_fingerprint.h"
#include "tired/io.h"
#include "tired/json.h"
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
int main(int argc, char **argv)
{
    int result = 1;
    TiredFileFingerprint input = {
        .exists = true,
        .device = (dev_t)-1,
        .inode = (ino_t)-1,
        .uid = (uid_t)-1,
        .gid = (gid_t)-1,
        .mode = 0600,
        .size = 16777216,
        .sha256 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    TiredFileFingerprint parsed = {0};
    TiredText encoded = {0}, schema = {0};
    struct json_object *document = NULL;
    TiredError error = {0};
    CHECK(argc == 2 && tired_read_file(argv[1], TIRED_INPUT_LIMIT, &schema, &error));
    CHECK(tired_json_parse(schema.data, schema.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_is_type(document, json_type_object));
    CHECK(tired_file_fingerprint_encode(&input, &encoded, &error));
    CHECK(strstr(encoded.data, "18446744073709551615") != NULL);
    CHECK(tired_file_fingerprint_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(tired_file_fingerprint_equal(&input, &parsed));
    const char *keys[] = {"schema_version", "exists", "device", "inode", "uid", "gid",
                          "mode",           "size",   "sha256", "extra"};
    const char *values[] = {"2",  "\"false\"", "-1",       "1.5",     "4294967296",
                            "-1", "4096",      "16777217", "\"bad\"", "true"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        CHECK(tired_json_parse(encoded.data, encoded.length, 4096, &document, &error));
        struct json_object *value = json_tokener_parse(values[i]);
        CHECK(value != NULL && json_object_object_add(document, keys[i], value) == 0);
        const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
        CHECK(!tired_file_fingerprint_parse(bad, strlen(bad), &parsed, &error));
        CHECK(tired_file_fingerprint_equal(&input, &parsed));
    }
    CHECK(tired_json_parse(encoded.data, encoded.length, 4096, &document, &error));
    json_object_object_del(document, "sha256");
    const char *missing = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_file_fingerprint_parse(missing, strlen(missing), &parsed, &error));
    const char *bad[] = {"{\"schema_version\":1,\"exists\":false,\"size\":0}",
                         "{\"schema_version\":1,\"exists\":true}",
                         "{\"schema_version\":1,\"exists\":false,\"exists\":true}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(!tired_file_fingerprint_parse(bad[i], strlen(bad[i]), &parsed, &error));
        CHECK(tired_file_fingerprint_equal(&input, &parsed));
    }
    input.mode = 010000;
    CHECK(!tired_file_fingerprint_encode(&input, &encoded, &error));
    CHECK(strstr(encoded.data, "18446744073709551615") != NULL);
    input.mode = 0600;
    input.sha256[0] = 'F';
    CHECK(!tired_file_fingerprint_encode(&input, &encoded, &error));
    input.exists = false;
    CHECK(tired_file_fingerprint_encode(&input, &encoded, &error));
    CHECK(strcmp(encoded.data, "{\"schema_version\":1,\"exists\":false}\n") == 0);
    CHECK(tired_file_fingerprint_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(!parsed.exists && parsed.size == 0 && parsed.sha256[0] == '\0');
    result = 0;
cleanup:
    json_object_put(document);
    tired_text_destroy(&encoded);
    tired_text_destroy(&schema);
    return result;
}
