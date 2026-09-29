#include "tired/service_metadata.h"
#include "tired/capture.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/name.h"
#include <assert.h>
#include <string.h>
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "service-metadata",
                           "Invalid service identity or revision metadata.", 0);
}
void tired_service_metadata_destroy(TiredServiceMetadata *metadata)
{
    if (metadata == NULL)
        return;
    tired_text_destroy(&metadata->unit_name);
    tired_text_destroy(&metadata->writer_version);
    *metadata = (TiredServiceMetadata){0};
}
bool tired_service_metadata_validate(const TiredServiceMetadata *metadata, TiredError *error)
{
    assert(metadata != NULL);
    if (!tired_uuid_valid(metadata->service_uuid, strnlen(metadata->service_uuid, 37)) ||
        !tired_uuid_valid(metadata->revision_uuid, strnlen(metadata->revision_uuid, 37)) ||
        !tired_uuid_valid(metadata->transaction_uuid, strnlen(metadata->transaction_uuid, 37)) ||
        strnlen(metadata->unit_sha256, 65) != 64 || metadata->created_usec == 0 ||
        metadata->updated_usec < metadata->created_usec || metadata->owner_uid == (uid_t)-1 ||
        metadata->invoking_uid == (uid_t)-1 || metadata->service_uid == (uid_t)-1 ||
        metadata->invoking_gid == (gid_t)-1 || metadata->service_gid == (gid_t)-1 ||
        (metadata->user_scope ? metadata->owner_uid != metadata->invoking_uid ||
                                    metadata->owner_uid != metadata->service_uid
                              : metadata->owner_uid != 0))
        return invalid(error);
    for (size_t i = 0; i < 64; ++i)
        if (!((metadata->unit_sha256[i] >= '0' && metadata->unit_sha256[i] <= '9') ||
              (metadata->unit_sha256[i] >= 'a' && metadata->unit_sha256[i] <= 'f')))
            return invalid(error);
    const TiredText *name = &metadata->unit_name, *version = &metadata->writer_version;
    if (name->data == NULL || name->length <= 8 || name->length > 208 ||
        memcmp(name->data + name->length - 8, ".service", 8) != 0 ||
        !tired_name_validate_base(name->data, name->length - 8, error) || version->data == NULL ||
        version->length == 0 || version->length > 64 ||
        !tired_validate_text(version->data, version->length, true, error))
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
bool tired_service_metadata_encode(const TiredServiceMetadata *metadata, TiredText *output,
                                   TiredError *error)
{
    assert(metadata != NULL && output != NULL);
    if (!tired_service_metadata_validate(metadata, error))
        return false;
    struct json_object *object = json_object_new_object();
    bool ok = false;
    if (object == NULL || !add(object, "schema_version", json_object_new_int(1)) ||
        !add(object, "service_uuid", json_object_new_string(metadata->service_uuid)) ||
        !add(object, "current_revision", json_object_new_string(metadata->revision_uuid)) ||
        !add(object, "last_transaction", json_object_new_string(metadata->transaction_uuid)) ||
        !add(object, "unit_sha256", json_object_new_string(metadata->unit_sha256)) ||
        !add(object, "unit_name",
             json_object_new_string_len(metadata->unit_name.data,
                                        (int)metadata->unit_name.length)) ||
        !add(object, "writer_version",
             json_object_new_string_len(metadata->writer_version.data,
                                        (int)metadata->writer_version.length)) ||
        !add(object, "created_usec", json_object_new_uint64(metadata->created_usec)) ||
        !add(object, "updated_usec", json_object_new_uint64(metadata->updated_usec)) ||
        !add(object, "owner_uid", json_object_new_uint64(metadata->owner_uid)) ||
        !add(object, "invoking_uid", json_object_new_uint64(metadata->invoking_uid)) ||
        !add(object, "invoking_gid", json_object_new_uint64(metadata->invoking_gid)) ||
        !add(object, "service_uid", json_object_new_uint64(metadata->service_uid)) ||
        !add(object, "service_gid", json_object_new_uint64(metadata->service_gid)) ||
        !add(object, "scope", json_object_new_string(metadata->user_scope ? "user" : "system")) ||
        !add(object, "creation_mode",
             json_object_new_string(metadata->interactive ? "interactive" : "headless")))
    {
        tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode service metadata.", 0);
        goto done;
    }
    const char *bytes = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot serialize service metadata.",
                        0);
    else
        ok = tired_text_set(output, bytes, strlen(bytes), 4096, error);
done:
    json_object_put(object);
    return ok;
}
static bool text(struct json_object *object, const char *key, TiredText *output, size_t limit,
                 TiredError *error)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) ||
        !json_object_is_type(value, json_type_string))
        return invalid(error);
    return tired_text_set(output, json_object_get_string(value),
                          (size_t)json_object_get_string_len(value), limit, error);
}
static bool fixed(struct json_object *object, const char *key, char *output, size_t length)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) ||
        !json_object_is_type(value, json_type_string) ||
        (size_t)json_object_get_string_len(value) != length)
        return false;
    memcpy(output, json_object_get_string(value), length + 1);
    return true;
}
static bool number(struct json_object *object, const char *key, uint64_t maximum, uint64_t *output,
                   TiredError *error)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value))
        return invalid(error);
    return tired_json_u64(value, 0, maximum, output, error);
}
bool tired_service_metadata_parse(const char *data, size_t length, TiredServiceMetadata *output,
                                  TiredError *error)
{
    assert(output != NULL);
    struct json_object *object = NULL;
    TiredServiceMetadata metadata = {0};
    TiredText scope = {0}, mode = {0};
    uint64_t version, uid[3], gid[2];
    bool ok = false;
    if (!tired_json_parse(data, length, 4096, &object, error))
        goto done;
    if (!json_object_is_type(object, json_type_object) || json_object_object_length(object) != 16 ||
        !number(object, "schema_version", 1, &version, error) || version != 1 ||
        !fixed(object, "service_uuid", metadata.service_uuid, 36) ||
        !fixed(object, "current_revision", metadata.revision_uuid, 36) ||
        !fixed(object, "last_transaction", metadata.transaction_uuid, 36) ||
        !fixed(object, "unit_sha256", metadata.unit_sha256, 64))
        goto bad;
    if (!text(object, "unit_name", &metadata.unit_name, 208, error) ||
        !text(object, "writer_version", &metadata.writer_version, 64, error) ||
        !text(object, "scope", &scope, 6, error) ||
        !text(object, "creation_mode", &mode, 11, error) ||
        !number(object, "created_usec", UINT64_MAX, &metadata.created_usec, error) ||
        !number(object, "updated_usec", UINT64_MAX, &metadata.updated_usec, error) ||
        !number(object, "owner_uid", (uint64_t)(uid_t)-1 - 1, &uid[0], error) ||
        !number(object, "invoking_uid", (uint64_t)(uid_t)-1 - 1, &uid[1], error) ||
        !number(object, "service_uid", (uint64_t)(uid_t)-1 - 1, &uid[2], error) ||
        !number(object, "invoking_gid", (uint64_t)(gid_t)-1 - 1, &gid[0], error) ||
        !number(object, "service_gid", (uint64_t)(gid_t)-1 - 1, &gid[1], error))
        goto done;
    if ((strcmp(scope.data, "user") != 0 && strcmp(scope.data, "system") != 0) ||
        (strcmp(mode.data, "interactive") != 0 && strcmp(mode.data, "headless") != 0))
        goto bad;
    metadata.user_scope = strcmp(scope.data, "user") == 0;
    metadata.interactive = strcmp(mode.data, "interactive") == 0;
    metadata.owner_uid = (uid_t)uid[0];
    metadata.invoking_uid = (uid_t)uid[1];
    metadata.service_uid = (uid_t)uid[2];
    metadata.invoking_gid = (gid_t)gid[0];
    metadata.service_gid = (gid_t)gid[1];
    if (!tired_service_metadata_validate(&metadata, error))
        goto done;
    tired_service_metadata_destroy(output);
    *output = metadata;
    metadata = (TiredServiceMetadata){0};
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    json_object_put(object);
    tired_service_metadata_destroy(&metadata);
    tired_text_destroy(&scope);
    tired_text_destroy(&mode);
    return ok;
}
