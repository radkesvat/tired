#include "tired/capture.h"
#include "tired/profile_snapshot.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static const char *const origins[] = {"bundled", "administrator", "user"};
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "profile-snapshot",
                           "Invalid historical profile snapshot.", 0);
}
void tired_profile_snapshot_destroy(TiredProfileSnapshot *snapshot)
{
    if (snapshot == NULL)
        return;
    tired_profile_destroy(&snapshot->profile);
    tired_text_destroy(&snapshot->source_path);
    free(snapshot->decisions);
    *snapshot = (TiredProfileSnapshot){0};
}
static bool provenance(const TiredProfileSnapshot *snapshot, TiredError *error)
{
    bool builtin = false;
    const TiredText *path = &snapshot->source_path;
    if (snapshot->source_origin == TIRED_PROFILE_BUNDLED && path->data != NULL &&
        path->length > 13 && path->length <= 4096 && strncmp(path->data, "builtin:", 8) == 0 &&
        strcmp(path->data + path->length - 5, ".json") == 0)
    {
        builtin = true;
        for (size_t i = 8; i < path->length - 5; ++i)
            if (!((path->data[i] >= 'a' && path->data[i] <= 'z') ||
                  (path->data[i] >= '0' && path->data[i] <= '9') || path->data[i] == '-' ||
                  path->data[i] == '_'))
                builtin = false;
    }
    if ((unsigned)snapshot->source_origin > TIRED_PROFILE_USER ||
        snapshot->source_path.data == NULL || snapshot->source_path.length == 0 ||
        snapshot->source_path.length > 4096 || (!builtin && snapshot->source_path.data[0] != '/') ||
        !tired_validate_text(snapshot->source_path.data, snapshot->source_path.length, true,
                             error) ||
        strnlen(snapshot->source_sha256, 65) != 64)
        return invalid(error);
    for (size_t i = 0; i < 64; ++i)
        if (!((snapshot->source_sha256[i] >= '0' && snapshot->source_sha256[i] <= '9') ||
              (snapshot->source_sha256[i] >= 'a' && snapshot->source_sha256[i] <= 'f')))
            return invalid(error);
    return true;
}
bool tired_profile_snapshot_parse(const char *data, size_t length, TiredProfileSnapshot *output,
                                  TiredError *error)
{
    assert(output != NULL);
    TiredProfileSnapshot snapshot = {0};
    struct json_object *document = NULL, *version = NULL, *profile = NULL, *path = NULL,
                       *digest = NULL, *origin = NULL, *selection = NULL, *decisions = NULL;
    uint64_t schema;
    bool ok = false;
    if (!tired_json_parse(data, length, TIRED_INPUT_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) != 7 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "profile", &profile) ||
        !json_object_is_type(profile, json_type_object) ||
        !json_object_object_get_ex(document, "source_path", &path) ||
        !json_object_is_type(path, json_type_string) ||
        !json_object_object_get_ex(document, "source_sha256", &digest) ||
        !json_object_is_type(digest, json_type_string) ||
        json_object_get_string_len(digest) != 64 ||
        !json_object_object_get_ex(document, "source_origin", &origin) ||
        !json_object_is_type(origin, json_type_string) ||
        !json_object_object_get_ex(document, "explicit_selection", &selection) ||
        !json_object_is_type(selection, json_type_boolean) ||
        !json_object_object_get_ex(document, "decisions", &decisions) ||
        !json_object_is_type(decisions, json_type_array))
        goto bad;
    const char *bytes = json_object_to_json_string_ext(profile, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        goto allocation;
    if (!tired_profile_parse(bytes, strlen(bytes), &snapshot.profile, error) ||
        !tired_text_set(&snapshot.source_path, json_object_get_string(path),
                        (size_t)json_object_get_string_len(path), 4096, error))
        goto done;
    memcpy(snapshot.source_sha256, json_object_get_string(digest), 65);
    unsigned source;
    for (source = 0; source <= TIRED_PROFILE_USER; ++source)
        if (strcmp(json_object_get_string(origin), origins[source]) == 0)
            break;
    snapshot.source_origin = (TiredProfileOrigin)source;
    snapshot.explicit_selection = json_object_get_boolean(selection);
    if (!provenance(&snapshot, error) ||
        json_object_array_length(decisions) != snapshot.profile.count)
        goto bad;
    snapshot.count = snapshot.profile.count;
    if (snapshot.count != 0)
    {
        snapshot.decisions = calloc(snapshot.count, sizeof(*snapshot.decisions));
        if (snapshot.decisions == NULL)
            goto allocation;
    }
    for (size_t i = 0; i < snapshot.count; ++i)
    {
        struct json_object *value = json_object_array_get_idx(decisions, i);
        if (!json_object_is_type(value, json_type_string))
            goto bad;
        unsigned disposition;
        for (disposition = 0; disposition <= TIRED_RECOMMENDATION_REQUIRED_CONFLICT; ++disposition)
            if (strcmp(json_object_get_string(value),
                       tired_recommendation_disposition_name(
                           (TiredRecommendationDisposition)disposition)) == 0)
                break;
        if (disposition > TIRED_RECOMMENDATION_REQUIRED_CONFLICT)
            goto bad;
        snapshot.decisions[i] = (TiredRecommendationDisposition)disposition;
    }
    tired_profile_snapshot_destroy(output);
    *output = snapshot;
    snapshot = (TiredProfileSnapshot){0};
    tired_error_clear(error);
    ok = true;
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot allocate profile snapshot.", 0);
    goto done;
bad:
    invalid(error);
done:
    json_object_put(document);
    tired_profile_snapshot_destroy(&snapshot);
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
bool tired_profile_snapshot_encode(const TiredProfileSnapshot *snapshot, TiredText *output,
                                   TiredError *error)
{
    assert(snapshot != NULL && output != NULL);
    if (!provenance(snapshot, error) || snapshot->profile.document == NULL ||
        snapshot->count != snapshot->profile.count ||
        (snapshot->count != 0 && snapshot->decisions == NULL))
        return invalid(error);
    struct json_object *document = json_object_new_object(), *decisions = json_object_new_array();
    TiredProfileSnapshot validated = {0};
    bool ok = false;
    if (document == NULL || decisions == NULL)
        goto allocation;
    for (size_t i = 0; i < snapshot->count; ++i)
    {
        if ((unsigned)snapshot->decisions[i] > TIRED_RECOMMENDATION_REQUIRED_CONFLICT)
        {
            invalid(error);
            goto done;
        }
        struct json_object *value =
            json_object_new_string(tired_recommendation_disposition_name(snapshot->decisions[i]));
        if (value == NULL || json_object_array_add(decisions, value) != 0)
        {
            json_object_put(value);
            goto allocation;
        }
    }
    bool inserted = add(document, "decisions", decisions);
    decisions = NULL;
    if (!inserted || !add(document, "schema_version", json_object_new_int(1)) ||
        !add(document, "profile", json_object_get(snapshot->profile.document)) ||
        !add(document, "source_path",
             json_object_new_string_len(snapshot->source_path.data,
                                        (int)snapshot->source_path.length)) ||
        !add(document, "source_sha256", json_object_new_string(snapshot->source_sha256)) ||
        !add(document, "source_origin", json_object_new_string(origins[snapshot->source_origin])) ||
        !add(document, "explicit_selection", json_object_new_boolean(snapshot->explicit_selection)))
        goto allocation;
    const char *bytes = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        goto allocation;
    if (!tired_profile_snapshot_parse(bytes, strlen(bytes), &validated, error))
        goto done;
    ok = tired_text_set(output, bytes, strlen(bytes), TIRED_INPUT_LIMIT, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode profile snapshot.", 0);
done:
    json_object_put(document);
    json_object_put(decisions);
    tired_profile_snapshot_destroy(&validated);
    return ok;
}
