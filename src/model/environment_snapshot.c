#include "tired/environment_snapshot.h"
#include "tired/encode.h"
#include "tired/json.h"
#include <assert.h>
#include <string.h>
static const char *const origins[] = {"default", "profile",  "config", "imported",
                                      "passed",  "explicit", "edited"};
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "environment-snapshot",
                           "Invalid private environment snapshot.", 0);
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
bool tired_environment_snapshot_parse(const char *data, size_t length,
                                      TiredEnvironment *environment, TiredCredentials *credentials,
                                      TiredError *error)
{
    assert(environment != NULL && credentials != NULL);
    TiredEnvironment next = {0};
    TiredCredentials next_credentials = {0};
    struct json_object *document = NULL, *version = NULL, *env = NULL, *refs = NULL;
    TiredBuffer assignment;
    tired_buffer_init(&assignment, TIRED_INPUT_LIMIT);
    bool ok = false;
    uint64_t schema;
    if (!tired_json_parse(data, length, TIRED_ENVIRONMENT_SNAPSHOT_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) != 3 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "environment", &env) ||
        !json_object_is_type(env, json_type_array) ||
        !json_object_object_get_ex(document, "credentials", &refs) ||
        !json_object_is_type(refs, json_type_array) ||
        json_object_array_length(env) > TIRED_ENVIRONMENT_COUNT_LIMIT ||
        json_object_array_length(refs) > TIRED_ENVIRONMENT_COUNT_LIMIT)
        goto bad;
    for (unsigned group = 0; group < 2; ++group)
    {
        struct json_object *array = group == 0 ? env : refs;
        for (size_t i = 0; i < json_object_array_length(array); ++i)
        {
            struct json_object *entry = json_object_array_get_idx(array, i), *sensitive = NULL;
            if (!json_object_is_type(entry, json_type_object) ||
                json_object_object_length(entry) != (group == 0 ? 4 : 2))
                goto bad;
            size_t name_length, value_length, origin_length;
            const char *name = string(entry, "name", &name_length);
            const char *value = string(entry, group == 0 ? "value" : "path", &value_length);
            if (name == NULL || value == NULL)
                goto bad;
            unsigned origin = 0;
            if (group == 0)
            {
                const char *source = string(entry, "origin", &origin_length);
                if (source == NULL || !json_object_object_get_ex(entry, "sensitive", &sensitive) ||
                    !json_object_is_type(sensitive, json_type_boolean) ||
                    tired_environment_find(&next, name, name_length) != NULL)
                    goto bad;
                for (origin = 0; origin <= TIRED_ENV_EDITED; ++origin)
                    if (strcmp(source, origins[origin]) == 0)
                        break;
                if (origin > TIRED_ENV_EDITED)
                    goto bad;
            }
            assignment.length = 0;
            if (!tired_buffer_append(&assignment, name, name_length, error) ||
                !tired_buffer_append(&assignment, "=", 1, error) ||
                !tired_buffer_append(&assignment, value, value_length, error))
                goto done;
            if (group == 0)
            {
                bool secret = json_object_get_boolean(sensitive);
                if (!tired_environment_set(&next, assignment.data, assignment.length,
                                           (TiredEnvironmentOrigin)origin, secret, error))
                    goto done;
                const TiredEnvironmentEntry *stored =
                    tired_environment_find(&next, name, name_length);
                if (stored == NULL || stored->sensitive != secret)
                    goto bad;
            }
            else
            {
                if (!tired_credentials_add(&next_credentials, assignment.data, assignment.length,
                                           error))
                    goto done;
                const TiredCredential *stored = &next_credentials.items[next_credentials.count - 1];
                if (stored->name.length != name_length ||
                    memcmp(stored->name.data, name, name_length) != 0 ||
                    stored->path.length != value_length ||
                    memcmp(stored->path.data, value, value_length) != 0)
                    goto bad;
            }
        }
    }
    tired_environment_destroy(environment);
    tired_credentials_destroy(credentials);
    *environment = next;
    *credentials = next_credentials;
    next = (TiredEnvironment){0};
    next_credentials = (TiredCredentials){0};
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    json_object_put(document);
    tired_buffer_destroy(&assignment);
    tired_environment_destroy(&next);
    tired_credentials_destroy(&next_credentials);
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
bool tired_environment_snapshot_encode(const TiredEnvironment *environment,
                                       const TiredCredentials *credentials, TiredText *output,
                                       TiredError *error)
{
    assert(environment != NULL && credentials != NULL && output != NULL);
    struct json_object *document = json_object_new_object(), *env = json_object_new_array(),
                       *refs = json_object_new_array(), *entry = NULL;
    TiredEnvironment validated = {0};
    TiredCredentials validated_refs = {0};
    bool ok = false;
    if (document == NULL || env == NULL || refs == NULL)
        goto allocation;
    if (environment->count > TIRED_ENVIRONMENT_COUNT_LIMIT ||
        credentials->count > TIRED_ENVIRONMENT_COUNT_LIMIT)
    {
        invalid(error);
        goto done;
    }
    for (unsigned group = 0; group < 2; ++group)
    {
        size_t count = group == 0 ? environment->count : credentials->count;
        for (size_t i = 0; i < count; ++i)
        {
            const TiredText *name =
                group == 0 ? &environment->items[i].name : &credentials->items[i].name;
            const TiredText *value =
                group == 0 ? &environment->items[i].value : &credentials->items[i].path;
            entry = json_object_new_object();
            if (entry == NULL ||
                !add(entry, "name", json_object_new_string_len(name->data, (int)name->length)) ||
                !add(entry, group == 0 ? "value" : "path",
                     json_object_new_string_len(value->data, (int)value->length)))
                goto allocation;
            if (group == 0)
            {
                const TiredEnvironmentEntry *item = &environment->items[i];
                if ((unsigned)item->origin > TIRED_ENV_EDITED)
                {
                    invalid(error);
                    goto done;
                }
                if (!add(entry, "origin", json_object_new_string(origins[item->origin])) ||
                    !add(entry, "sensitive", json_object_new_boolean(item->sensitive)))
                    goto allocation;
            }
            if (json_object_array_add(group == 0 ? env : refs, entry) != 0)
                goto allocation;
            entry = NULL;
        }
    }
    bool inserted = add(document, "environment", env);
    env = NULL;
    if (!inserted)
        goto allocation;
    inserted = add(document, "credentials", refs);
    refs = NULL;
    if (!inserted || !add(document, "schema_version", json_object_new_int(1)))
        goto allocation;
    const char *bytes = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (bytes == NULL)
        goto allocation;
    if (!tired_environment_snapshot_parse(bytes, strlen(bytes), &validated, &validated_refs, error))
        goto done;
    ok = tired_text_set(output, bytes, strlen(bytes), TIRED_ENVIRONMENT_SNAPSHOT_LIMIT, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation",
                    "Cannot encode private environment snapshot.", 0);
done:
    json_object_put(entry);
    json_object_put(env);
    json_object_put(refs);
    json_object_put(document);
    tired_environment_destroy(&validated);
    tired_credentials_destroy(&validated_refs);
    return ok;
}
