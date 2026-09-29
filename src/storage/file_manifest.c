#include "tired/file_manifest.h"
#include "tired/io.h"
#include "tired/json.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "file-manifest",
                           "File manifest contains invalid, inconsistent or incomplete changes.",
                           0);
}
static bool reference(const char value[37], bool required)
{
    return required ? tired_uuid_valid(value, strnlen(value, 37)) : value[0] == '\0';
}
static bool same_name(const TiredText *a, const TiredText *b)
{
    return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}
static bool same_target(const TiredFileTarget *a, const TiredFileTarget *b)
{
    if (a->role != b->role)
        return false;
    return a->role == TIRED_FILE_TARGET_RECORD ||
           (a->role == TIRED_FILE_TARGET_UNIT && same_name(&a->unit_name, &b->unit_name)) ||
           (a->role == TIRED_FILE_TARGET_ENVIRONMENT &&
            strcmp(a->revision_uuid, b->revision_uuid) == 0);
}
bool tired_file_manifest_validate(const TiredFileManifest *manifest, TiredError *error)
{
    assert(manifest != NULL);
    TiredText encoded = {0};
    bool ok = tired_transaction_record_encode(&manifest->prepared, &encoded, error);
    tired_text_destroy(&encoded);
    if (!ok)
        return false;
    if (manifest->prepared.sequence != 1 || manifest->prepared.action != TIRED_ACTION_PREPARE ||
        manifest->prepared.state != TIRED_ACTION_COMPLETED ||
        manifest->count > TIRED_FILE_CHANGE_LIMIT ||
        (manifest->count != 0 && manifest->files == NULL))
        return invalid(error);
    TiredTransactionOperation operation = manifest->prepared.operation;
    size_t units = 0, records = 0, removed = 0, added = 0;
    for (size_t i = 0; i < manifest->count; ++i)
    {
        const TiredFileChange *file = &manifest->files[i];
        if (!tired_file_target_validate(&file->target, error) ||
            strcmp(file->target.service_uuid, manifest->prepared.service_uuid) != 0 ||
            !reference(file->staging_uuid, file->after.exists) ||
            !reference(file->rollback_uuid, file->before.exists) ||
            (!file->before.exists && !file->after.exists))
            return invalid(error);
        ok = tired_file_fingerprint_encode(&file->before, &encoded, error) &&
             tired_file_fingerprint_encode(&file->after, &encoded, error);
        tired_text_destroy(&encoded);
        if (!ok)
            return false;
        if (file->after.exists &&
            file->after.mode != (file->target.role == TIRED_FILE_TARGET_UNIT ? 0644U : 0600U))
            return invalid(error);
        if (file->before.exists && file->after.exists &&
            file->before.device == file->after.device && file->before.inode == file->after.inode)
            return invalid(error);
        for (size_t j = 0; j < i; ++j)
            if (same_target(&file->target, &manifest->files[j].target))
                return invalid(error);
        bool creates = !file->before.exists && file->after.exists;
        bool deletes = file->before.exists && !file->after.exists;
        if (file->target.role == TIRED_FILE_TARGET_UNIT)
        {
            ++units;
            if (operation == TIRED_TRANSACTION_RENAME)
            {
                if (creates && same_name(&file->target.unit_name, &manifest->prepared.unit_name))
                    ++added;
                else if (deletes &&
                         !same_name(&file->target.unit_name, &manifest->prepared.unit_name))
                    ++removed;
                else
                    return invalid(error);
            }
            else if (operation > TIRED_TRANSACTION_RESTORE ||
                     !same_name(&file->target.unit_name, &manifest->prepared.unit_name))
                return invalid(error);
        }
        else if (file->target.role == TIRED_FILE_TARGET_RECORD)
            ++records;
        else if (operation > TIRED_TRANSACTION_RESTORE ||
                 (operation == TIRED_TRANSACTION_REMOVE ? !deletes : !creates))
            return invalid(error);
        if (file->target.role != TIRED_FILE_TARGET_ENVIRONMENT &&
            !(file->target.role == TIRED_FILE_TARGET_UNIT && operation == TIRED_TRANSACTION_RENAME))
        {
            if (operation == TIRED_TRANSACTION_CREATE   ? !creates
                : operation == TIRED_TRANSACTION_REMOVE ? !deletes
                                                        : creates || deletes)
                return invalid(error);
        }
    }
    if (operation <= TIRED_TRANSACTION_RESTORE)
    {
        if (records != 1 || units != (operation == TIRED_TRANSACTION_RENAME ? 2U : 1U) ||
            (operation == TIRED_TRANSACTION_RENAME && (added != 1 || removed != 1)))
            return invalid(error);
    }
    else if (units != 0 || records > 1)
        return invalid(error);
    tired_error_clear(error);
    return true;
}
void tired_file_manifest_destroy(TiredFileManifest *manifest)
{
    if (manifest == NULL)
        return;
    tired_transaction_record_destroy(&manifest->prepared);
    for (size_t i = 0; i < manifest->count; ++i)
        tired_text_destroy(&manifest->files[i].target.unit_name);
    free(manifest->files);
    *manifest = (TiredFileManifest){0};
}
static const char *string(struct json_object *object, const char *key, size_t *length)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) ||
        !json_object_is_type(value, json_type_string))
        return NULL;
    *length = (size_t)json_object_get_string_len(value);
    return json_object_get_string(value);
}
static bool uuid_field(struct json_object *object, const char *key, char output[37])
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value))
        return false;
    if (json_object_is_type(value, json_type_null))
        return true;
    if (!json_object_is_type(value, json_type_string) || json_object_get_string_len(value) != 36)
        return false;
    memcpy(output, json_object_get_string(value), 36);
    output[36] = '\0';
    return tired_uuid_valid(output, 36);
}
static bool target_read(struct json_object *object, const char service[37], TiredFileTarget *target,
                        TiredError *error)
{
    if (!json_object_is_type(object, json_type_object))
        return invalid(error);
    size_t length;
    const char *role = string(object, "role", &length);
    if (role == NULL)
        return invalid(error);
    if (strcmp(role, "unit") == 0)
        target->role = TIRED_FILE_TARGET_UNIT;
    else if (strcmp(role, "environment") == 0)
        target->role = TIRED_FILE_TARGET_ENVIRONMENT;
    else if (strcmp(role, "record") == 0)
        target->role = TIRED_FILE_TARGET_RECORD;
    else
        return invalid(error);
    if (json_object_object_length(object) != (target->role == TIRED_FILE_TARGET_RECORD ? 1 : 2))
        return invalid(error);
    memcpy(target->service_uuid, service, 37);
    if (target->role == TIRED_FILE_TARGET_UNIT)
    {
        const char *name = string(object, "unit_name", &length);
        if (name == NULL)
            return invalid(error);
        if (!tired_text_set(&target->unit_name, name, length, 208, error))
            return false;
    }
    else if (target->role == TIRED_FILE_TARGET_ENVIRONMENT &&
             !uuid_field(object, "revision_uuid", target->revision_uuid))
        return invalid(error);
    return tired_file_target_validate(target, error);
}
static const char *nested(struct json_object *object, const char *key, TiredError *error)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value))
    {
        invalid(error);
        return NULL;
    }
    const char *encoded = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    if (encoded == NULL)
        tired_error_set(error, TIRED_INTERNAL, "allocation",
                        "Cannot inspect nested file manifest data.", 0);
    return encoded;
}
bool tired_file_manifest_parse(const char *data, size_t length, TiredFileManifest *output,
                               TiredError *error)
{
    assert(output != NULL);
    struct json_object *document = NULL, *version = NULL, *files = NULL;
    TiredFileManifest manifest = {0};
    bool ok = false;
    uint64_t schema;
    if (!tired_json_parse(data, length, TIRED_FILE_MANIFEST_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) != 3 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "files", &files) ||
        !json_object_is_type(files, json_type_array))
        goto schema_error;
    const char *prepared = nested(document, "prepared", error);
    if (prepared == NULL)
        goto done;
    if (!tired_transaction_record_parse(prepared, strlen(prepared), &manifest.prepared, error))
        goto done;
    manifest.count = json_object_array_length(files);
    if (manifest.count > TIRED_FILE_CHANGE_LIMIT)
    {
        manifest.count = 0;
        goto schema_error;
    }
    if (manifest.count != 0)
    {
        manifest.files = calloc(manifest.count, sizeof(*manifest.files));
        if (manifest.files == NULL)
        {
            manifest.count = 0;
            tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate file manifest.",
                            0);
            goto done;
        }
    }
    for (size_t i = 0; i < manifest.count; ++i)
    {
        struct json_object *entry = json_object_array_get_idx(files, i), *target = NULL;
        TiredFileChange *file = &manifest.files[i];
        if (!json_object_is_type(entry, json_type_object) ||
            json_object_object_length(entry) != 5 ||
            !json_object_object_get_ex(entry, "target", &target) ||
            !uuid_field(entry, "staging_uuid", file->staging_uuid) ||
            !uuid_field(entry, "rollback_uuid", file->rollback_uuid))
            goto schema_error;
        if (!target_read(target, manifest.prepared.service_uuid, &file->target, error))
            goto done;
        const char *before = nested(entry, "before", error), *after = nested(entry, "after", error);
        if (before == NULL || after == NULL)
            goto done;
        if (!tired_file_fingerprint_parse(before, strlen(before), &file->before, error) ||
            !tired_file_fingerprint_parse(after, strlen(after), &file->after, error))
            goto done;
    }
    if (!tired_file_manifest_validate(&manifest, error))
        goto done;
    tired_file_manifest_destroy(output);
    *output = manifest;
    manifest = (TiredFileManifest){0};
    tired_error_clear(error);
    ok = true;
    goto done;
schema_error:
    invalid(error);
done:
    json_object_put(document);
    tired_file_manifest_destroy(&manifest);
    return ok;
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
static bool reference_add(struct json_object *object, const char *key, const char value[37])
{
    return value[0] == '\0' ? json_object_object_add(object, key, NULL) == 0
                            : add(object, key, json_object_new_string(value));
}
static struct json_object *fingerprint_json(const TiredFileFingerprint *fingerprint,
                                            TiredError *error)
{
    TiredText encoded = {0};
    struct json_object *value = NULL;
    if (tired_file_fingerprint_encode(fingerprint, &encoded, error))
        (void)tired_json_parse(encoded.data, encoded.length, 4096, &value, error);
    tired_text_destroy(&encoded);
    return value;
}
bool tired_file_manifest_encode(const TiredFileManifest *manifest, TiredText *output,
                                TiredError *error)
{
    assert(manifest != NULL && output != NULL);
    if (!tired_file_manifest_validate(manifest, error))
        return false;
    TiredText prepared_text = {0};
    struct json_object *document = json_object_new_object(), *files = json_object_new_array();
    struct json_object *prepared = NULL, *entry = NULL, *target = NULL;
    bool ok = false;
    if (document == NULL || files == NULL)
        goto allocation;
    if (!tired_transaction_record_encode(&manifest->prepared, &prepared_text, error) ||
        !tired_json_parse(prepared_text.data, prepared_text.length, 4096, &prepared, error))
        goto done;
    for (size_t i = 0; i < manifest->count; ++i)
    {
        const TiredFileChange *file = &manifest->files[i];
        static const char *const roles[] = {"unit", "environment", "record"};
        entry = json_object_new_object();
        target = json_object_new_object();
        if (entry == NULL || target == NULL ||
            !add(target, "role", json_object_new_string(roles[file->target.role])))
            goto allocation;
        if (file->target.role == TIRED_FILE_TARGET_UNIT &&
            !add(target, "unit_name",
                 json_object_new_string_len(file->target.unit_name.data,
                                            (int)file->target.unit_name.length)))
            goto allocation;
        if (file->target.role == TIRED_FILE_TARGET_ENVIRONMENT &&
            !add(target, "revision_uuid", json_object_new_string(file->target.revision_uuid)))
            goto allocation;
        bool inserted = add(entry, "target", target);
        target = NULL;
        if (!inserted || !add(entry, "before", fingerprint_json(&file->before, error)) ||
            !add(entry, "after", fingerprint_json(&file->after, error)) ||
            !reference_add(entry, "staging_uuid", file->staging_uuid) ||
            !reference_add(entry, "rollback_uuid", file->rollback_uuid))
            goto allocation;
        if (json_object_array_add(files, entry) != 0)
            goto allocation;
        entry = NULL;
    }
    bool inserted = add(document, "prepared", prepared);
    prepared = NULL;
    if (!inserted)
        goto allocation;
    inserted = add(document, "files", files);
    files = NULL;
    if (!inserted || !add(document, "schema_version", json_object_new_int(1)))
        goto allocation;
    const char *encoded = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (encoded == NULL)
        goto allocation;
    ok = tired_text_set(output, encoded, strlen(encoded), TIRED_FILE_MANIFEST_LIMIT, error);
    if (ok)
        tired_error_clear(error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate encoded file manifest.",
                    0);
done:
    json_object_put(target);
    json_object_put(entry);
    json_object_put(prepared);
    json_object_put(files);
    json_object_put(document);
    tired_text_destroy(&prepared_text);
    return ok;
}
