#include "tired/service_record.h"
#include "tired/file_target.h"
#include "tired/io.h"
#include "tired/name.h"
#include "tired/render.h"
#include <assert.h>
#include <string.h>
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "service-record",
                           "Service record contains incomplete or inconsistent data.", 0);
}
static bool path_valid(const TiredText *path, TiredError *error)
{
    return path->data != NULL && path->length > 0 && path->length < 4096 && path->data[0] == '/' &&
           tired_validate_text(path->data, path->length, true, error);
}
void tired_service_record_destroy(TiredServiceRecord *record)
{
    if (record == NULL)
        return;
    tired_service_metadata_destroy(&record->metadata);
    tired_spec_destroy(&record->spec);
    tired_environment_destroy(&record->environment);
    tired_credentials_destroy(&record->credentials);
    tired_executable_evidence_destroy(&record->executable);
    tired_profile_snapshot_destroy(&record->profile);
    tired_text_destroy(&record->unit_path);
    tired_text_destroy(&record->environment_path);
    tired_text_list_destroy(&record->external_config_paths);
    tired_text_list_destroy(&record->former_unit_names);
    *record = (TiredServiceRecord){0};
}
static bool related(const TiredServiceRecord *record, TiredError *error)
{
    const TiredFieldValue *name = &record->spec.fields[TIRED_FIELD_NAME],
                          *executable = &record->spec.fields[TIRED_FIELD_EXECUTABLE],
                          *argv = &record->spec.fields[TIRED_FIELD_ARGV];
    if (!tired_field_has_value(name) || !tired_field_has_value(executable) ||
        !tired_field_has_value(argv) ||
        !tired_field_has_value(&record->spec.fields[TIRED_FIELD_START]) ||
        !tired_field_has_value(&record->spec.fields[TIRED_FIELD_ENABLE]) ||
        record->metadata.unit_name.length != name->value.text.length + 8 ||
        memcmp(record->metadata.unit_name.data, name->value.text.data, name->value.text.length) !=
            0 ||
        record->metadata.user_scope !=
            tired_spec_choice_is(&record->spec, TIRED_FIELD_SCOPE, "user") ||
        record->review.argument_count != argv->value.list.count ||
        executable->value.text.length != record->executable.lexical_path.length ||
        memcmp(executable->value.text.data, record->executable.lexical_path.data,
               executable->value.text.length) != 0 ||
        !path_valid(&record->unit_path, error) ||
        (record->environment.count != 0 && !record->has_environment) ||
        (record->linger_requested && !record->metadata.user_scope) ||
        (record->has_profile && !record->metadata.user_scope &&
         record->profile.source_origin == TIRED_PROFILE_USER))
        return invalid(error);
    if (record->has_environment)
    {
        if (!tired_uuid_valid(record->environment_revision,
                              strnlen(record->environment_revision, 37)) ||
            !path_valid(&record->environment_path, error) ||
            strnlen(record->environment_sha256, 65) != 64)
            return invalid(error);
        for (size_t i = 0; i < 64; ++i)
            if (!((record->environment_sha256[i] >= '0' && record->environment_sha256[i] <= '9') ||
                  (record->environment_sha256[i] >= 'a' && record->environment_sha256[i] <= 'f')))
                return invalid(error);
    }
    else if (record->environment_path.data != NULL || record->environment_revision[0] != '\0' ||
             record->environment_sha256[0] != '\0')
        return invalid(error);
    if (record->external_config_paths.count > 256)
        return invalid(error);
    for (size_t i = 0; i < record->external_config_paths.count; ++i)
        if (!path_valid(&record->external_config_paths.items[i], error))
            return invalid(error);
    for (size_t i = 0; i < record->former_unit_names.count; ++i)
    {
        const TiredText *name = &record->former_unit_names.items[i];
        if (name->length <= 8 || name->length > 208 ||
            strcmp(name->data + name->length - 8, ".service") != 0 ||
            !tired_name_validate_base(name->data, name->length - 8, error))
            return invalid(error);
    }
    TiredText rendered = {0};
    bool ok = tired_render_unit(&record->spec, record->metadata.service_uuid,
                                record->has_environment ? &record->environment_path : NULL,
                                &record->credentials, &rendered, error);
    tired_text_destroy(&rendered);
    return ok;
}
static bool nested(struct json_object *document, const char *key, TiredText *bytes,
                   TiredError *error)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(document, key, &value) ||
        !json_object_is_type(value, json_type_object))
        return invalid(error);
    const char *text = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    if (text == NULL)
        return tired_error_set(error, TIRED_INTERNAL, "allocation",
                               "Cannot inspect nested service data.", 0);
    return tired_text_set(bytes, text, strlen(text), TIRED_SERVICE_RECORD_LIMIT, error);
}
static bool read_text(struct json_object *document, const char *key, TiredText *output,
                      TiredError *error)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(document, key, &value) ||
        !json_object_is_type(value, json_type_string))
        return invalid(error);
    return tired_text_set(output, json_object_get_string(value),
                          (size_t)json_object_get_string_len(value), 4096, error);
}
static bool inputs_parse(struct json_object *document, TiredServiceRecord *record,
                         TiredError *error)
{
    struct json_object *inputs = NULL, *environment = NULL;
    if (!json_object_object_get_ex(document, "inputs", &inputs) ||
        !json_object_object_get_ex(inputs, "environment", &environment) ||
        !json_object_is_type(environment, json_type_array))
        return invalid(error);
    /* Persisted metadata deliberately has no plaintext assignment values. */
    for (size_t i = 0; i < json_object_array_length(environment); ++i)
    {
        struct json_object *entry = json_object_array_get_idx(environment, i), *value = NULL;
        if (!json_object_object_get_ex(entry, "value", &value) || value != NULL)
            return invalid(error);
        struct json_object *empty = json_object_new_string("");
        if (empty == NULL || json_object_object_add(entry, "value", empty) != 0)
        {
            json_object_put(empty);
            return false;
        }
    }
    const char *bytes = json_object_to_json_string_ext(inputs, JSON_C_TO_STRING_PLAIN);
    return bytes != NULL &&
           tired_environment_snapshot_parse(bytes, strlen(bytes), &record->environment,
                                            &record->credentials, error);
}
bool tired_service_record_parse(const char *data, size_t length, TiredServiceRecord *output,
                                TiredError *error)
{
    assert(output != NULL);
    TiredServiceRecord record = {0};
    TiredText bytes = {0}, revision = {0}, digest = {0};
    struct json_object *document = NULL, *version = NULL, *profile = NULL, *environment = NULL,
                       *configs = NULL, *former = NULL, *linger = NULL;
    uint64_t schema;
    bool ok = false;
    if (!tired_json_parse(data, length, TIRED_SERVICE_RECORD_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) < 10 || json_object_object_length(document) > 12 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "profile", &profile) ||
        !json_object_object_get_ex(document, "owned_environment", &environment) ||
        !json_object_object_get_ex(document, "external_config_paths", &configs) ||
        !json_object_is_type(configs, json_type_array))
        goto bad;
    if (json_object_object_get_ex(document, "former_unit_names", &former))
    {
        if (!json_object_is_type(former, json_type_array))
            goto bad;
    }
    bool has_linger = json_object_object_get_ex(document, "linger_requested", &linger);
    if ((has_linger && !json_object_is_type(linger, json_type_boolean)) ||
        json_object_object_length(document) != 10 + (former != NULL) + has_linger)
        goto bad;
    if (!nested(document, "metadata", &bytes, error) ||
        !tired_service_metadata_parse(bytes.data, bytes.length, &record.metadata, error) ||
        !nested(document, "model", &bytes, error) ||
        !tired_spec_parse(bytes.data, bytes.length, &record.spec, error) ||
        !inputs_parse(document, &record, error) || !nested(document, "review", &bytes, error) ||
        !tired_review_snapshot_parse(bytes.data, bytes.length, &record.review, error) ||
        !nested(document, "executable", &bytes, error) ||
        !tired_executable_evidence_parse(bytes.data, bytes.length, &record.executable, error) ||
        !read_text(document, "unit_path", &record.unit_path, error))
        goto done;
    record.linger_requested = has_linger
                                  ? json_object_get_boolean(linger)
                                  : record.spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean;
    if (profile != NULL)
    {
        if (!nested(document, "profile", &bytes, error) ||
            !tired_profile_snapshot_parse(bytes.data, bytes.length, &record.profile, error))
            goto done;
        record.has_profile = true;
    }
    if (environment != NULL)
    {
        if (!json_object_is_type(environment, json_type_object) ||
            json_object_object_length(environment) != 3 ||
            !read_text(environment, "revision", &revision, error) ||
            !read_text(environment, "sha256", &digest, error) ||
            !read_text(environment, "path", &record.environment_path, error) ||
            revision.length != 36 || digest.length != 64)
            goto bad;
        memcpy(record.environment_revision, revision.data, 37);
        memcpy(record.environment_sha256, digest.data, 65);
        record.has_environment = true;
    }
    for (size_t i = 0; i < json_object_array_length(configs); ++i)
    {
        struct json_object *value = json_object_array_get_idx(configs, i);
        if (!json_object_is_type(value, json_type_string))
            goto bad;
        if (!tired_text_list_append(&record.external_config_paths, json_object_get_string(value),
                                    (size_t)json_object_get_string_len(value), 256,
                                    TIRED_INPUT_LIMIT, error))
            goto done;
    }
    for (size_t i = 0; former != NULL && i < json_object_array_length(former); ++i)
    {
        struct json_object *value = json_object_array_get_idx(former, i);
        if (!json_object_is_type(value, json_type_string) ||
            !tired_text_list_append(&record.former_unit_names, json_object_get_string(value),
                                    (size_t)json_object_get_string_len(value), 256,
                                    TIRED_INPUT_LIMIT, error))
            goto bad;
    }
    if (!related(&record, error))
        goto done;
    tired_service_record_destroy(output);
    *output = record;
    record = (TiredServiceRecord){0};
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    json_object_put(document);
    tired_service_record_destroy(&record);
    tired_text_destroy(&bytes);
    tired_text_destroy(&revision);
    tired_text_destroy(&digest);
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
static bool store(struct json_object *object, const char *key, const TiredText *bytes,
                  TiredError *error)
{
    struct json_object *value = NULL;
    if (!tired_json_parse(bytes->data, bytes->length, TIRED_SERVICE_RECORD_LIMIT, &value, error))
        return false;
    if (add(object, key, value))
        return true;
    return tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot compose service record.",
                           0);
}
bool tired_service_record_encode(const TiredServiceRecord *record, TiredText *output,
                                 TiredError *error)
{
    assert(record != NULL && output != NULL);
    struct json_object *document = json_object_new_object(), *environment = NULL,
                       *configs = json_object_new_array();
    TiredText bytes = {0};
    TiredServiceRecord validated = {0};
    bool ok = false;
    if (document == NULL || configs == NULL)
        goto allocation;
    if (!tired_service_metadata_encode(&record->metadata, &bytes, error) ||
        !store(document, "metadata", &bytes, error) ||
        !tired_spec_encode(&record->spec, &bytes, error) ||
        !store(document, "model", &bytes, error) ||
        !tired_environment_snapshot_encode(&record->environment, &record->credentials, &bytes,
                                           error) ||
        !store(document, "inputs", &bytes, error) ||
        !tired_review_snapshot_encode(&record->review, &bytes, error) ||
        !store(document, "review", &bytes, error) ||
        !tired_executable_evidence_encode(&record->executable, &bytes, error) ||
        !store(document, "executable", &bytes, error))
        goto done;
    struct json_object *inputs = NULL, *assignments = NULL;
    if (!json_object_object_get_ex(document, "inputs", &inputs) ||
        !json_object_object_get_ex(inputs, "environment", &assignments))
        goto done;
    for (size_t i = 0; i < json_object_array_length(assignments); ++i)
        if (json_object_object_add(json_object_array_get_idx(assignments, i), "value", NULL) != 0)
            goto allocation;
    if (record->has_profile)
    {
        if (!tired_profile_snapshot_encode(&record->profile, &bytes, error) ||
            !store(document, "profile", &bytes, error))
            goto done;
    }
    else if (json_object_object_add(document, "profile", NULL) != 0)
        goto allocation;
    if (!related(record, error))
        goto done;
    if (record->has_environment)
    {
        environment = json_object_new_object();
        if (environment == NULL ||
            !add(environment, "revision", json_object_new_string(record->environment_revision)) ||
            !add(environment, "sha256", json_object_new_string(record->environment_sha256)) ||
            !add(environment, "path",
                 json_object_new_string_len(record->environment_path.data,
                                            (int)record->environment_path.length)))
            goto allocation;
        bool inserted = add(document, "owned_environment", environment);
        environment = NULL;
        if (!inserted)
            goto allocation;
    }
    else if (json_object_object_add(document, "owned_environment", NULL) != 0)
        goto allocation;
    for (size_t i = 0; i < record->external_config_paths.count; ++i)
    {
        const TiredText *path = &record->external_config_paths.items[i];
        struct json_object *value = json_object_new_string_len(path->data, (int)path->length);
        if (value == NULL || json_object_array_add(configs, value) != 0)
        {
            json_object_put(value);
            goto allocation;
        }
    }
    bool inserted = add(document, "external_config_paths", configs);
    configs = NULL;
    if (!inserted || !add(document, "schema_version", json_object_new_int(1)) ||
        !add(document, "unit_path",
             json_object_new_string_len(record->unit_path.data, (int)record->unit_path.length)))
        goto allocation;
    struct json_object *former = json_object_new_array();
    if (former == NULL)
        goto allocation;
    for (size_t i = 0; i < record->former_unit_names.count; ++i)
    {
        struct json_object *name = json_object_new_string(record->former_unit_names.items[i].data);
        if (name == NULL || json_object_array_add(former, name) != 0)
        {
            json_object_put(name);
            json_object_put(former);
            goto allocation;
        }
    }
    if (!add(document, "former_unit_names", former) ||
        !add(document, "linger_requested", json_object_new_boolean(record->linger_requested)))
        goto allocation;
    const char *serialized = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (serialized == NULL)
        goto allocation;
    if (!tired_service_record_parse(serialized, strlen(serialized), &validated, error))
        goto done;
    ok = tired_text_set(output, serialized, strlen(serialized), TIRED_SERVICE_RECORD_LIMIT, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode service record.", 0);
done:
    json_object_put(environment);
    json_object_put(configs);
    json_object_put(document);
    tired_text_destroy(&bytes);
    tired_service_record_destroy(&validated);
    return ok;
}
static bool matches_path(const TiredResolvedFile *resolved, const TiredText *path)
{
    bool slash = resolved->directory.length != 1;
    size_t offset = resolved->directory.length + (slash ? 1U : 0U);
    return path->length == offset + resolved->name.length &&
           memcmp(path->data, resolved->directory.data, resolved->directory.length) == 0 &&
           (!slash || path->data[offset - 1] == '/') &&
           memcmp(path->data + offset, resolved->name.data, resolved->name.length) == 0;
}
bool tired_service_record_check_layout(const TiredServiceRecord *record, const TiredLayout *layout,
                                       TiredError *error)
{
    assert(record != NULL && layout != NULL);
    if (layout->user_scope != record->metadata.user_scope ||
        !path_valid(&record->unit_path, error) ||
        (record->has_environment && !path_valid(&record->environment_path, error)))
        return invalid(error);
    TiredFileTarget target = {.role = TIRED_FILE_TARGET_UNIT,
                              .unit_name = record->metadata.unit_name};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    TiredResolvedFile resolved = {0};
    bool ok = tired_file_target_resolve(layout, &target, &resolved, error);
    if (ok && !matches_path(&resolved, &record->unit_path))
        ok = invalid(error);
    tired_resolved_file_destroy(&resolved);
    if (ok && record->has_environment)
    {
        target.role = TIRED_FILE_TARGET_ENVIRONMENT;
        target.unit_name = (TiredText){0};
        memcpy(target.revision_uuid, record->environment_revision, 37);
        ok = tired_file_target_resolve(layout, &target, &resolved, error);
        if (ok && !matches_path(&resolved, &record->environment_path))
            ok = invalid(error);
        tired_resolved_file_destroy(&resolved);
    }
    if (ok)
        tired_error_clear(error);
    return ok;
}
