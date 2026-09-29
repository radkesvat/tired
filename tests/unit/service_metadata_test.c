#include "tired/json.h"
#include "tired/service_metadata.h"
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
    TiredServiceMetadata source = {
        .service_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
        .revision_uuid = "11234567-89ab-4cde-8fab-0123456789ab",
        .transaction_uuid = "21234567-89ab-4cde-8fab-0123456789ab",
        .unit_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .unit_name = {.data = "relay.service", .length = 13},
        .writer_version = {.data = "0.1.0", .length = 5},
        .created_usec = 1,
        .updated_usec = UINT64_MAX,
        .invoking_uid = 1000,
        .invoking_gid = 1000,
        .service_uid = 1001,
        .service_gid = 1002};
    TiredServiceMetadata parsed = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *object = NULL;
    CHECK(tired_service_metadata_encode(&source, &encoded, &error));
    CHECK(tired_service_metadata_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.updated_usec == UINT64_MAX && parsed.created_usec == 1 && parsed.owner_uid == 0);
    CHECK(parsed.invoking_uid == 1000 && parsed.service_uid == 1001 && parsed.service_gid == 1002);
    CHECK(!parsed.user_scope && !parsed.interactive &&
          strcmp(parsed.unit_name.data, "relay.service") == 0);
    CHECK(tired_service_metadata_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    source.owner_uid = 1000;
    CHECK(!tired_service_metadata_validate(&source, &error));
    source.user_scope = true;
    CHECK(!tired_service_metadata_validate(&source, &error));
    source.service_uid = 1000;
    source.interactive = true;
    CHECK(tired_service_metadata_encode(&source, &encoded, &error));
    CHECK(tired_service_metadata_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.user_scope && parsed.interactive && parsed.owner_uid == parsed.service_uid);
    source.updated_usec = 0;
    CHECK(!tired_service_metadata_encode(&source, &again, &error));
    CHECK(strstr(again.data, "system") != NULL);
    source.updated_usec = 2;
    source.unit_sha256[0] = 'A';
    CHECK(!tired_service_metadata_validate(&source, &error));
    source.unit_sha256[0] = '0';
    source.invoking_gid = (gid_t)-1;
    CHECK(!tired_service_metadata_validate(&source, &error));
    CHECK(tired_json_parse(encoded.data, encoded.length, 4096, &object, &error));
    CHECK(json_object_object_add(object, "owner_uid", json_object_new_uint64(UINT64_MAX)) == 0);
    const char *bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_service_metadata_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.owner_uid == 1000 && parsed.updated_usec == UINT64_MAX);
    CHECK(json_object_object_add(object, "owner_uid", json_object_new_int(1000)) == 0);
    CHECK(json_object_object_add(object, "extra", json_object_new_int(1)) == 0);
    bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_service_metadata_parse(bad, strlen(bad), &parsed, &error));
    result = 0;
cleanup:
    json_object_put(object);
    tired_service_metadata_destroy(&parsed);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
