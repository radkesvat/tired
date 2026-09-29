#include "tired/file_manifest.h"
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
static TiredFileFingerprint snapshot(ino_t inode, unsigned mode)
{
    return (TiredFileFingerprint){
        .exists = true,
        .device = 1,
        .inode = inode,
        .mode = mode,
        .size = 3,
        .sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
}
int main(int argc, char **argv)
{
    int result = 1;
    const char *service = "fedcba98-7654-4321-abcd-fedcba987654";
    const char *artifact = "11234567-89ab-4cde-8fab-0123456789ab";
    TiredFileChange files[4] = {0};
    TiredFileManifest source = {
        .prepared = {.transaction_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
                     .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
                     .approved_sha256 =
                         "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                     .unit_name = {.data = "relay.service", .length = 13},
                     .sequence = 1,
                     .operation = TIRED_TRANSACTION_CREATE,
                     .action = TIRED_ACTION_PREPARE,
                     .state = TIRED_ACTION_COMPLETED},
        .files = files,
        .count = 2};
    TiredFileManifest parsed = {0};
    TiredText encoded = {0}, again = {0}, schema = {0};
    struct json_object *document = NULL, *entries = NULL, *target = NULL;
    TiredError error = {0};
    CHECK(argc == 2 && tired_read_file(argv[1], TIRED_INPUT_LIMIT, &schema, &error));
    CHECK(tired_json_parse(schema.data, schema.length, TIRED_INPUT_LIMIT, &document, &error));
    for (size_t i = 0; i < 3; ++i)
    {
        memcpy(files[i].target.service_uuid, service, 37);
        memcpy(files[i].staging_uuid, artifact, 37);
    }
    files[0].target.role = TIRED_FILE_TARGET_UNIT;
    files[0].target.unit_name = source.prepared.unit_name;
    files[0].after = snapshot(10, 0644);
    files[1].target.role = TIRED_FILE_TARGET_RECORD;
    files[1].after = snapshot(20, 0600);
    CHECK(tired_file_manifest_encode(&source, &encoded, &error));
    CHECK(tired_file_manifest_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.count == 2 &&
          tired_file_fingerprint_equal(&parsed.files[0].after, &files[0].after));
    CHECK(strcmp(parsed.files[0].target.service_uuid, service) == 0);
    CHECK(tired_file_manifest_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    source.prepared.sequence = 2;
    CHECK(!tired_file_manifest_validate(&source, &error));
    source.prepared.sequence = 1;
    files[0].after.mode = 0600;
    CHECK(!tired_file_manifest_validate(&source, &error));
    files[0].after.mode = 0644;
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "files", &entries));
    CHECK(json_object_object_get_ex(json_object_array_get_idx(entries, 0), "target", &target));
    CHECK(json_object_object_add(target, "destination", json_object_new_string("/etc/passwd")) ==
          0);
    const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_file_manifest_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.count == 2 && parsed.prepared.operation == TIRED_TRANSACTION_CREATE);
    source.count = 1;
    CHECK(!tired_file_manifest_validate(&source, &error));
    source.count = 2;
    files[0].staging_uuid[0] = '\0';
    CHECK(!tired_file_manifest_validate(&source, &error));
    memcpy(files[0].staging_uuid, artifact, 37);
    files[0].before = snapshot(1, 0644);
    memcpy(files[0].rollback_uuid, artifact, 37);
    CHECK(!tired_file_manifest_validate(&source, &error));
    source.prepared.operation = TIRED_TRANSACTION_EDIT;
    files[1].before = snapshot(2, 0600);
    memcpy(files[1].rollback_uuid, artifact, 37);
    CHECK(tired_file_manifest_validate(&source, &error));
    files[0].after.inode = files[0].before.inode;
    CHECK(!tired_file_manifest_validate(&source, &error));
    files[0].after.inode = 10;
    files[2].target.role = TIRED_FILE_TARGET_ENVIRONMENT;
    memcpy(files[2].target.revision_uuid, artifact, 37);
    files[2].after = snapshot(30, 0600);
    source.count = 3;
    CHECK(tired_file_manifest_encode(&source, &encoded, &error));
    CHECK(tired_file_manifest_parse(encoded.data, encoded.length, &parsed, &error));
    files[3] = files[2];
    files[3].after.inode = 31;
    source.count = 4;
    CHECK(!tired_file_manifest_validate(&source, &error));
    source.count = 3;
    files[2].target.service_uuid[0] = 'a';
    CHECK(!tired_file_manifest_validate(&source, &error));
    files[2].target.service_uuid[0] = 'f';
    files[2].before = snapshot(3, 0600);
    memcpy(files[2].rollback_uuid, artifact, 37);
    CHECK(!tired_file_manifest_validate(&source, &error));
    source.count = 2;
    source.prepared.operation = TIRED_TRANSACTION_REMOVE;
    files[0].after = files[1].after = (TiredFileFingerprint){0};
    files[0].staging_uuid[0] = files[1].staging_uuid[0] = '\0';
    CHECK(tired_file_manifest_encode(&source, &encoded, &error));
    CHECK(tired_file_manifest_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(!parsed.files[0].after.exists && !parsed.files[1].after.exists);
    source.prepared.operation = TIRED_TRANSACTION_RESTORE;
    files[0].after = snapshot(10, 0644);
    files[1].after = snapshot(20, 0600);
    memcpy(files[0].staging_uuid, artifact, 37);
    memcpy(files[1].staging_uuid, artifact, 37);
    CHECK(tired_file_manifest_validate(&source, &error));
    /* Rename records both removal of the old unit and creation of the new one. */
    source.count = 3;
    source.prepared.operation = TIRED_TRANSACTION_RENAME;
    source.prepared.unit_name = (TiredText){.data = "new.service", .length = 11};
    files[2] = files[1];
    files[1] = files[0];
    files[1].target.unit_name = source.prepared.unit_name;
    files[1].before = (TiredFileFingerprint){0};
    files[1].rollback_uuid[0] = '\0';
    files[0].after = (TiredFileFingerprint){0};
    files[0].staging_uuid[0] = '\0';
    CHECK(tired_file_manifest_encode(&source, &encoded, &error));
    CHECK(tired_file_manifest_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.count == 3 &&
          strcmp(parsed.files[0].target.unit_name.data, "relay.service") == 0 &&
          strcmp(parsed.files[1].target.unit_name.data, "new.service") == 0);
    files[1].target.unit_name = (TiredText){.data = "third.service", .length = 13};
    CHECK(!tired_file_manifest_validate(&source, &error));
    CHECK(!tired_file_manifest_encode(&source, &again, &error));
    CHECK(strstr(again.data, "relay.service") != NULL);
    source.count = 0;
    source.prepared.operation = TIRED_TRANSACTION_START;
    CHECK(tired_file_manifest_encode(&source, &encoded, &error));
    CHECK(tired_file_manifest_parse(encoded.data, encoded.length, &parsed, &error) &&
          parsed.count == 0);
    result = 0;
cleanup:
    json_object_put(document);
    tired_file_manifest_destroy(&parsed);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    tired_text_destroy(&schema);
    return result;
}
