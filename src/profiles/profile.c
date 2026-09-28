#include "tired/profile.h"
#include "tired/capture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool failure(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "profile-schema",
                           "Profile has an unknown, missing, or invalid schema field.", 0);
}
static bool keys(struct json_object *object, const char *const *allowed, size_t count,
                 TiredError *error)
{
    if (!json_object_is_type(object, json_type_object))
        return failure(error);
    json_object_object_foreach(object, key, value)
    {
        (void)value;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
            if (strcmp(key, allowed[i]) == 0)
                found = true;
        if (!found)
            return failure(error);
    }
    return true;
}
static struct json_object *get(struct json_object *object, const char *key)
{
    if (!json_object_is_type(object, json_type_object))
        return NULL;
    struct json_object *value = NULL;
    (void)json_object_object_get_ex(object, key, &value);
    return value;
}
static const char *text(struct json_object *object, const char *key, size_t maximum)
{
    struct json_object *value = get(object, key);
    if (!json_object_is_type(value, json_type_string))
        return NULL;
    size_t length = (size_t)json_object_get_string_len(value);
    return length > 0 && length <= maximum ? json_object_get_string(value) : NULL;
}
static bool identifier(const char *name)
{
    if (name == NULL || name[0] == '\0' || strlen(name) > 80)
        return false;
    for (size_t i = 0; name[i] != '\0'; ++i)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') ||
              (i != 0 && (name[i] == '-' || name[i] == '_'))))
            return false;
    return true;
}
static bool member(const char *value, const char *const *choices, size_t count)
{
    if (value == NULL)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(value, choices[i]) == 0)
            return true;
    return false;
}
static bool date(const char *value)
{
    if (value == NULL || strlen(value) != 10 || value[4] != '-' || value[7] != '-')
        return false;
    unsigned year = 0, month = 0, day = 0;
    for (size_t i = 0; i < 10; ++i)
    {
        if (i == 4 || i == 7)
            continue;
        if (value[i] < '0' || value[i] > '9')
            return false;
        if (i < 4)
            year = year * 10 + (unsigned)(value[i] - '0');
        else if (i < 7)
            month = month * 10 + (unsigned)(value[i] - '0');
        else
            day = day * 10 + (unsigned)(value[i] - '0');
    }
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year == 0 || month == 0 || month > 12)
        return false;
    unsigned maximum =
        days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return day > 0 && day <= maximum;
}

static bool sources_valid(struct json_object *sources, TiredError *error)
{
    if (!json_object_is_type(sources, json_type_array) || json_object_array_length(sources) > 128)
        return failure(error);
    const char *allowed[] = {"id", "url", "checked_at", "source_kind"};
    const char *kinds[] = {"tired-policy", "upstream-documentation", "source-audit",
                           "tested-recommendation"};
    for (size_t i = 0; i < json_object_array_length(sources); ++i)
    {
        struct json_object *source = json_object_array_get_idx(sources, i);
        const char *id = text(source, "id", 80), *url = text(source, "url", 4096);
        if (!keys(source, allowed, 4, error) || !identifier(id) || url == NULL ||
            strncmp(url, "https://", 8) != 0 || url[8] == '\0' ||
            !date(text(source, "checked_at", 10)) ||
            !member(text(source, "source_kind", 64), kinds, 4))
            return failure(error);
        for (size_t j = 0; j < i; ++j)
            if (strcmp(id, text(json_object_array_get_idx(sources, j), "id", 80)) == 0)
                return failure(error);
    }
    return true;
}

static bool conditions_valid(struct json_object *condition, unsigned depth, TiredError *error)
{
    if (depth > 8 || !json_object_is_type(condition, json_type_object) ||
        json_object_object_length(condition) != 1)
        return failure(error);
    json_object_object_foreach(condition, key, value)
    {
        if (strcmp(key, "all") == 0 || strcmp(key, "any") == 0)
        {
            if (!json_object_is_type(value, json_type_array) ||
                json_object_array_length(value) == 0 || json_object_array_length(value) > 16)
                return failure(error);
            for (size_t i = 0; i < json_object_array_length(value); ++i)
                if (!conditions_valid(json_object_array_get_idx(value, i), depth + 1, error))
                    return false;
        }
        else
        {
            if (strcmp(key, "nofile_at_least") == 0)
            {
                uint64_t minimum;
                if (!tired_json_u64(value, 1, UINT64_MAX - 1, &minimum, error))
                    return false;
                continue;
            }
            if (!json_object_is_type(value, json_type_string) ||
                json_object_get_string_len(value) == 0 || json_object_get_string_len(value) > 4096)
                return failure(error);
            const char *string = json_object_get_string(value);
            if (strcmp(key, "scope") == 0)
            {
                const char *choices[] = {"system", "user"};
                if (!member(string, choices, 2))
                    return failure(error);
            }
            else if (strcmp(key, "type") == 0)
            {
                const char *choices[] = {"exec", "simple", "notify", "forking", "oneshot"};
                if (!member(string, choices, 5))
                    return failure(error);
            }
            else if (strcmp(key, "feature") == 0)
            {
                const char *choices[] = {"credentials", "memory-max", "cpu-quota", "tasks-max",
                                         "ambient-capabilities"};
                if (!member(string, choices, 5))
                    return failure(error);
            }
            else if (strcmp(key, "inspection") == 0)
            {
                const char *choices[] = {"executable-regular", "working-directory-accessible"};
                if (!member(string, choices, 2))
                    return failure(error);
            }
            else if (strcmp(key, "argument") != 0)
                return failure(error);
        }
    }
    return true;
}

static bool typed_value(const TiredField *field, struct json_object *input,
                        TiredRecommendation *rec, TiredError *error)
{
    TiredServiceSpec temporary = {0};
    bool ok = false;
    if (field->kind == TIRED_FIELD_LIST)
    {
        if (!json_object_is_type(input, json_type_array))
            return failure(error);
        ok = tired_spec_clear_list(&temporary, field->id, TIRED_ORIGIN_PROFILE, error);
        for (size_t i = 0; ok && i < json_object_array_length(input); ++i)
        {
            struct json_object *entry = json_object_array_get_idx(input, i);
            if (!json_object_is_type(entry, json_type_string))
            {
                ok = failure(error);
                break;
            }
            ok = tired_spec_append(&temporary, field->id, json_object_get_string(entry),
                                   (size_t)json_object_get_string_len(entry), TIRED_ORIGIN_PROFILE,
                                   error);
        }
    }
    else
    {
        bool valid = false;
        switch (field->kind)
        {
        case TIRED_FIELD_BOOL:
            valid = json_object_is_type(input, json_type_boolean);
            break;
        case TIRED_FIELD_INTEGER:
            valid = json_object_is_type(input, json_type_int);
            break;
        case TIRED_FIELD_LIMIT:
        case TIRED_FIELD_SIGNAL:
            valid = json_object_is_type(input, json_type_int) ||
                    json_object_is_type(input, json_type_string);
            break;
        default:
            valid = json_object_is_type(input, json_type_string);
            break;
        }
        if (!valid)
            return failure(error);
        const char *value = json_object_get_string(input);
        ok = value != NULL && tired_spec_set(&temporary, field->id, value, strlen(value),
                                             TIRED_ORIGIN_PROFILE, false, error);
    }
    if (ok)
    {
        rec->value = temporary.fields[field->id];
        temporary.fields[field->id] = (TiredFieldValue){0};
    }
    tired_spec_destroy(&temporary);
    return ok;
}

void tired_profile_destroy(TiredProfile *profile)
{
    if (profile == NULL)
        return;
    for (size_t i = 0; i < profile->count; ++i)
    {
        TiredServiceSpec temporary = {0};
        temporary.fields[profile->recommendations[i].field] = profile->recommendations[i].value;
        tired_spec_destroy(&temporary);
    }
    free(profile->recommendations);
    tired_text_list_destroy(&profile->basenames);
    json_object_put(profile->document);
    *profile = (TiredProfile){0};
}

bool tired_profile_parse(const char *data, size_t length, TiredProfile *profile, TiredError *error)
{
    assert(profile != NULL);
    TiredProfile parsed = {0};
    if (!tired_json_parse(data, length, TIRED_PROFILE_LIMIT, &parsed.document, error))
        return false;
    struct json_object *root = parsed.document;
    const char *top[] = {"schema_version", "id",      "name",          "revision",
                         "summary",        "match",   "compatibility", "recommendations",
                         "advisories",     "sources", "replaces"};
    uint64_t schema;
    if (!keys(root, top, sizeof(top) / sizeof(top[0]), error) ||
        !tired_json_u64(get(root, "schema_version"), 1, 1, &schema, error) ||
        !tired_json_u64(get(root, "revision"), 1, UINT32_MAX, &parsed.revision, error))
        goto fail;
    parsed.id = text(root, "id", 80);
    parsed.name = text(root, "name", 256);
    parsed.summary = text(root, "summary", 4096);
    if (!identifier(parsed.id) || strcmp(parsed.id, "auto") == 0 ||
        strcmp(parsed.id, "none") == 0 || parsed.name == NULL || parsed.summary == NULL)
        goto schema_fail;
    struct json_object *replacement = NULL;
    if (json_object_object_get_ex(root, "replaces", &replacement) &&
        (!json_object_is_type(replacement, json_type_string) ||
         !identifier(json_object_get_string(replacement)) ||
         strcmp(parsed.id, json_object_get_string(replacement)) != 0))
        goto schema_fail;
    const char *match_keys[] = {"executable_basenames", "case_sensitive"};
    struct json_object *match = get(root, "match"), *basenames = get(match, "executable_basenames"),
                       *sensitive = get(match, "case_sensitive");
    if (!keys(match, match_keys, 2, error) || !json_object_is_type(sensitive, json_type_boolean) ||
        !json_object_is_type(basenames, json_type_array) ||
        json_object_array_length(basenames) > 64)
        goto schema_fail;
    parsed.case_sensitive = json_object_get_boolean(sensitive);
    for (size_t i = 0; i < json_object_array_length(basenames); ++i)
    {
        struct json_object *item = json_object_array_get_idx(basenames, i);
        if (!json_object_is_type(item, json_type_string))
            goto schema_fail;
        const char *name = json_object_get_string(item);
        size_t size = (size_t)json_object_get_string_len(item);
        if (size == 0 || size > 255 || strchr(name, '/') != NULL ||
            !tired_validate_text(name, size, true, error))
            goto schema_fail;
        if (!tired_text_list_append(&parsed.basenames, name, size, 64, 16384, error))
            goto fail;
    }
    const char *compat_keys[] = {"systemd_min", "application_version", "version_policy"};
    struct json_object *compat = get(root, "compatibility"), *version = NULL;
    if (!keys(compat, compat_keys, 3, error) ||
        !tired_json_u64(get(compat, "systemd_min"), 249, UINT32_MAX, &parsed.systemd_min, error) ||
        !json_object_object_get_ex(compat, "application_version", &version))
        goto schema_fail;
    parsed.version_restricted = version != NULL;
    if (version != NULL &&
        (!json_object_is_type(version, json_type_string) ||
         json_object_get_string_len(version) == 0 || json_object_get_string_len(version) > 128))
        goto schema_fail;
    const char *policies[] = {"version-independent-advice", "exact-version"};
    const char *policy = text(compat, "version_policy", 64);
    if (!member(policy, policies, 2) ||
        (parsed.version_restricted != (strcmp(policy, "exact-version") == 0)))
        goto schema_fail;
    struct json_object *sources = get(root, "sources");
    if (!sources_valid(sources, error))
        goto fail;
    struct json_object *advisories = get(root, "advisories");
    if (!json_object_is_type(advisories, json_type_array) ||
        json_object_array_length(advisories) > 128)
        goto schema_fail;
    const char *advisory_keys[] = {"code", "message"};
    for (size_t i = 0; i < json_object_array_length(advisories); ++i)
    {
        struct json_object *advisory = json_object_array_get_idx(advisories, i);
        if (!keys(advisory, advisory_keys, 2, error) || !identifier(text(advisory, "code", 80)) ||
            text(advisory, "message", 4096) == NULL)
            goto schema_fail;
    }
    struct json_object *recommendations = get(root, "recommendations");
    if (!json_object_is_type(recommendations, json_type_array) ||
        json_object_array_length(recommendations) > 256)
        goto schema_fail;
    size_t count = json_object_array_length(recommendations);
    if (count != 0 &&
        (parsed.recommendations = calloc(count, sizeof(*parsed.recommendations))) == NULL)
    {
        tired_error_set(error, TIRED_INTERNAL, "allocation",
                        "Cannot allocate profile recommendations.", 0);
        goto fail;
    }
    const char *rec_keys[] = {"field", "value",  "scope",      "apply",     "strength",
                              "risk",  "reason", "source_ids", "conditions"};
    const char *apply[] = {"automatic", "suggestion"};
    const char *strength[] = {"default", "recommended", "required-under-stated-conditions"};
    const char *risks[] = {"normal", "resource-change", "privilege-change", "behavior-change"};
    for (size_t i = 0; i < count; ++i)
    {
        struct json_object *item = json_object_array_get_idx(recommendations, i);
        if (!keys(item, rec_keys, 9, error))
            goto fail;
        const char *field_name = text(item, "field", 80);
        const TiredField *field =
            field_name == NULL ? NULL : tired_field_find(field_name, strlen(field_name));
        if (field == NULL || field->id == TIRED_FIELD_NAME || field->id == TIRED_FIELD_SCOPE ||
            field->id == TIRED_FIELD_RUN_AS || field->id == TIRED_FIELD_GROUP ||
            field->id == TIRED_FIELD_SUPPLEMENTARY_GROUPS || field->id == TIRED_FIELD_EXECUTABLE ||
            field->id == TIRED_FIELD_ARGV || field->id == TIRED_FIELD_WORKING_DIRECTORY)
            goto schema_fail;
        TiredRecommendation *rec = &parsed.recommendations[i];
        rec->field = field->id;
        rec->reason = text(item, "reason", 4096);
        rec->strength = text(item, "strength", 64);
        rec->risk = text(item, "risk", 64);
        const char *how = text(item, "apply", 32);
        if (rec->reason == NULL || !member(how, apply, 2) || !member(rec->strength, strength, 3) ||
            !member(rec->risk, risks, 4))
            goto schema_fail;
        rec->automatic = strcmp(how, "automatic") == 0;
        if (rec->automatic && (field->id == TIRED_FIELD_AMBIENT_CAPABILITIES ||
                               field->id == TIRED_FIELD_CAPABILITY_BOUNDING_SET))
            goto schema_fail;
        struct json_object *scopes = get(item, "scope");
        if (!json_object_is_type(scopes, json_type_array) ||
            json_object_array_length(scopes) == 0 || json_object_array_length(scopes) > 2)
            goto schema_fail;
        for (size_t j = 0; j < json_object_array_length(scopes); ++j)
        {
            struct json_object *scope = json_object_array_get_idx(scopes, j);
            if (!json_object_is_type(scope, json_type_string))
                goto schema_fail;
            const char *name = json_object_get_string(scope);
            unsigned bit = strcmp(name, "system") == 0 ? 1U : strcmp(name, "user") == 0 ? 2U : 0U;
            if (bit == 0 || (rec->scopes & bit) != 0)
                goto schema_fail;
            rec->scopes |= bit;
        }
        if (json_object_object_get_ex(item, "conditions", &rec->conditions) &&
            !conditions_valid(rec->conditions, 1, error))
            goto fail;
        if (strcmp(rec->strength, "required-under-stated-conditions") == 0 &&
            rec->conditions == NULL)
            goto schema_fail;
        struct json_object *ids = get(item, "source_ids");
        if (!json_object_is_type(ids, json_type_array) || json_object_array_length(ids) == 0 ||
            json_object_array_length(ids) > 128)
            goto schema_fail;
        for (size_t j = 0; j < json_object_array_length(ids); ++j)
        {
            struct json_object *id = json_object_array_get_idx(ids, j);
            bool found = false;
            if (!json_object_is_type(id, json_type_string))
                goto schema_fail;
            for (size_t k = 0; k < json_object_array_length(sources); ++k)
                if (strcmp(json_object_get_string(id),
                           text(json_object_array_get_idx(sources, k), "id", 80)) == 0)
                    found = true;
            if (!found)
                goto schema_fail;
        }
        if (!typed_value(field, get(item, "value"), rec, error))
            goto fail;
        ++parsed.count;
    }
    tired_profile_destroy(profile);
    *profile = parsed;
    tired_error_clear(error);
    return true;
schema_fail:
    failure(error);
fail:
    tired_profile_destroy(&parsed);
    return false;
}

bool tired_profile_matches(const TiredProfile *profile, const TiredText *executable)
{
    assert(profile != NULL && executable != NULL);
    const char *name = strrchr(executable->data, '/');
    name = name == NULL ? executable->data : name + 1;
    size_t length = strlen(name);
    for (size_t i = 0; i < profile->basenames.count; ++i)
    {
        const TiredText *candidate = &profile->basenames.items[i];
        if (candidate->length != length)
            continue;
        bool same = true;
        for (size_t j = 0; j < length; ++j)
        {
            unsigned char a = (unsigned char)name[j], b = (unsigned char)candidate->data[j];
            if (!profile->case_sensitive)
            {
                if (a >= 'A' && a <= 'Z')
                    a = (unsigned char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z')
                    b = (unsigned char)(b - 'A' + 'a');
            }
            if (a != b)
                same = false;
        }
        if (same)
            return true;
    }
    return false;
}
