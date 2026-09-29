#include "tired/profile_merge.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

const char *tired_recommendation_disposition_name(TiredRecommendationDisposition disposition)
{
    static const char *names[] = {
        "applied",           "scope-mismatch",  "condition-false",
        "condition-unknown", "version-unknown", "incompatible",
        "suggestion",        "user-override",   "conflicting-recommendations",
        "required-conflict"};
    return (unsigned)disposition < sizeof(names) / sizeof(names[0]) ? names[disposition]
                                                                    : "invalid";
}
void tired_profile_merge_destroy(TiredProfileMerge *merge)
{
    if (merge == NULL)
        return;
    tired_spec_destroy(&merge->spec);
    free(merge->decisions);
    *merge = (TiredProfileMerge){0};
}

static TiredFact condition(struct json_object *object, const TiredServiceSpec *spec,
                           const TiredProfileContext *context)
{
    if (object == NULL)
        return TIRED_FACT_TRUE;
    json_object_object_foreach(object, key, value)
    {
        if (strcmp(key, "all") == 0 || strcmp(key, "any") == 0)
        {
            bool all = strcmp(key, "all") == 0, unknown = false;
            for (size_t i = 0; i < json_object_array_length(value); ++i)
            {
                TiredFact fact = condition(json_object_array_get_idx(value, i), spec, context);
                if (all && fact == TIRED_FACT_FALSE)
                    return TIRED_FACT_FALSE;
                if (!all && fact == TIRED_FACT_TRUE)
                    return TIRED_FACT_TRUE;
                if (fact == TIRED_FACT_UNKNOWN)
                    unknown = true;
            }
            return unknown ? TIRED_FACT_UNKNOWN : all ? TIRED_FACT_TRUE : TIRED_FACT_FALSE;
        }
        if (strcmp(key, "nofile_at_least") == 0)
        {
            if (!context->nofile_known)
                return TIRED_FACT_UNKNOWN;
            TiredLimit minimum = {.value = json_object_get_uint64(value)};
            return tired_limit_le(minimum, context->nofile_ceiling) ? TIRED_FACT_TRUE
                                                                    : TIRED_FACT_FALSE;
        }
        const char *text = json_object_get_string(value);
        if (strcmp(key, "scope") == 0 || strcmp(key, "type") == 0)
        {
            TiredFieldId id = strcmp(key, "scope") == 0 ? TIRED_FIELD_SCOPE : TIRED_FIELD_TYPE;
            if (!tired_field_has_value(&spec->fields[id]))
                return TIRED_FACT_UNKNOWN;
            return tired_spec_choice_is(spec, id, text) ? TIRED_FACT_TRUE : TIRED_FACT_FALSE;
        }
        if (strcmp(key, "argument") == 0)
        {
            if (!tired_field_has_value(&spec->fields[TIRED_FIELD_ARGV]))
                return TIRED_FACT_UNKNOWN;
            const TiredTextList *args = &spec->fields[TIRED_FIELD_ARGV].value.list;
            for (size_t i = 1; i < args->count; ++i)
                if (strcmp(args->items[i].data, text) == 0)
                    return TIRED_FACT_TRUE;
            return TIRED_FACT_FALSE;
        }
        if (strcmp(key, "feature") == 0)
        {
            const char *names[] = {"credentials", "memory-max", "cpu-quota", "tasks-max",
                                   "ambient-capabilities"};
            for (size_t i = 0; i < 5; ++i)
                if (strcmp(names[i], text) == 0)
                    return context->features[i];
        }
        if (strcmp(key, "inspection") == 0)
        {
            const char *names[] = {"executable-regular", "working-directory-accessible"};
            for (size_t i = 0; i < 2; ++i)
                if (strcmp(names[i], text) == 0)
                    return context->inspections[i];
        }
    }
    return TIRED_FACT_UNKNOWN;
}

static bool equal_value(TiredFieldId id, const TiredFieldValue *a, const TiredFieldValue *b)
{
    switch (tired_field_get(id)->kind)
    {
    case TIRED_FIELD_TEXT:
        return a->value.text.length == b->value.text.length &&
               memcmp(a->value.text.data, b->value.text.data, a->value.text.length) == 0;
    case TIRED_FIELD_CHOICE:
        return a->value.choice == b->value.choice;
    case TIRED_FIELD_BOOL:
        return a->value.boolean == b->value.boolean;
    case TIRED_FIELD_INTEGER:
        return a->value.integer == b->value.integer;
    case TIRED_FIELD_DURATION:
        return a->value.microseconds == b->value.microseconds;
    case TIRED_FIELD_LIMIT:
        return a->value.limit.infinity == b->value.limit.infinity &&
               (a->value.limit.infinity || a->value.limit.value == b->value.limit.value);
    case TIRED_FIELD_MODE:
        return a->value.mode == b->value.mode;
    case TIRED_FIELD_QUOTA:
        return a->value.quota == b->value.quota;
    case TIRED_FIELD_SIGNAL:
        return a->value.signal_number == b->value.signal_number;
    case TIRED_FIELD_LIST:
        if (a->value.list.count != b->value.list.count)
            return false;
        for (size_t i = 0; i < a->value.list.count; ++i)
            if (a->value.list.items[i].length != b->value.list.items[i].length ||
                strcmp(a->value.list.items[i].data, b->value.list.items[i].data) != 0)
                return false;
        return true;
    }
    return false;
}

static TiredRecommendationDisposition decide(const TiredProfile *profile,
                                             const TiredRecommendation *rec,
                                             const TiredServiceSpec *spec,
                                             const TiredProfileContext *context)
{
    unsigned scope = tired_spec_choice_is(spec, TIRED_FIELD_SCOPE, "user") ? 2U : 1U;
    if ((scope & rec->scopes) == 0)
        return TIRED_RECOMMENDATION_SCOPE;
    if (context->systemd_version == 0)
        return TIRED_RECOMMENDATION_VERSION_UNKNOWN;
    if (context->systemd_version < profile->systemd_min)
        return TIRED_RECOMMENDATION_INCOMPATIBLE;
    if (profile->version_restricted)
    {
        if (context->application_version == NULL)
            return TIRED_RECOMMENDATION_VERSION_UNKNOWN;
        struct json_object *compat = NULL, *version = NULL;
        (void)json_object_object_get_ex(profile->document, "compatibility", &compat);
        (void)json_object_object_get_ex(compat, "application_version", &version);
        if (strcmp(context->application_version, json_object_get_string(version)) != 0)
            return TIRED_RECOMMENDATION_INCOMPATIBLE;
    }
    TiredFact fact = condition(rec->conditions, spec, context);
    if (fact == TIRED_FACT_FALSE)
        return TIRED_RECOMMENDATION_CONDITION_FALSE;
    if (fact == TIRED_FACT_UNKNOWN)
        return TIRED_RECOMMENDATION_CONDITION_UNKNOWN;
    if (!rec->automatic)
        return TIRED_RECOMMENDATION_SUGGESTION;
    if (spec->fields[rec->field].origin == TIRED_ORIGIN_USER)
    {
        if (strcmp(rec->strength, "required-under-stated-conditions") == 0 &&
            (spec->fields[rec->field].inherit ||
             !equal_value(rec->field, &spec->fields[rec->field], &rec->value)))
            return TIRED_RECOMMENDATION_REQUIRED_CONFLICT;
        return TIRED_RECOMMENDATION_USER_OVERRIDE;
    }
    return TIRED_RECOMMENDATION_APPLIED;
}

bool tired_profile_merge(const TiredProfile *profile, const TiredServiceSpec *input,
                         const TiredProfileContext *context, TiredProfileMerge *output,
                         TiredError *error)
{
    assert(profile != NULL && input != NULL && context != NULL && output != NULL);
    TiredProfileMerge result = {0};
    if (profile->count != 0)
    {
        result.decisions = calloc(profile->count, sizeof(*result.decisions));
        if (result.decisions == NULL)
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot allocate recommendation decisions.", 0);
    }
    result.count = profile->count;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (!tired_spec_copy_field(&result.spec, input, (TiredFieldId)i, error))
            goto fail;
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        for (size_t i = 0; i < profile->count; ++i)
        {
            const TiredRecommendation *rec = &profile->recommendations[i];
            if ((rec->field == TIRED_FIELD_TYPE) != (pass == 0))
                continue;
            result.decisions[i] = decide(profile, rec, &result.spec, context);
        }
        /* Compare all eligible values before modifying any field in this pass. */
        bool conflicts[TIRED_FIELD_COUNT] = {0};
        for (size_t i = 0; i < profile->count; ++i)
        {
            const TiredRecommendation *a = &profile->recommendations[i];
            if ((a->field == TIRED_FIELD_TYPE) != (pass == 0) ||
                result.decisions[i] != TIRED_RECOMMENDATION_APPLIED)
                continue;
            for (size_t j = i + 1; j < profile->count; ++j)
            {
                const TiredRecommendation *b = &profile->recommendations[j];
                if (a->field == b->field && result.decisions[j] == TIRED_RECOMMENDATION_APPLIED &&
                    !equal_value(a->field, &a->value, &b->value))
                    conflicts[a->field] = true;
            }
        }
        for (size_t i = 0; i < profile->count; ++i)
        {
            const TiredRecommendation *rec = &profile->recommendations[i];
            if ((rec->field == TIRED_FIELD_TYPE) != (pass == 0) ||
                result.decisions[i] != TIRED_RECOMMENDATION_APPLIED)
                continue;
            if (conflicts[rec->field])
            {
                result.decisions[i] = TIRED_RECOMMENDATION_CONFLICT;
                continue;
            }
            TiredServiceSpec borrowed = {0};
            borrowed.fields[rec->field] = rec->value;
            if (!tired_spec_copy_field(&result.spec, &borrowed, rec->field, error))
                goto fail;
        }
    }
    if (!tired_spec_resolve_scope(&result.spec, error) ||
        !tired_spec_resolve_retry(&result.spec, error) ||
        !tired_spec_validate_scalars(&result.spec, error))
        goto fail;
    tired_profile_merge_destroy(output);
    *output = result;
    tired_error_clear(error);
    return true;
fail:
    tired_profile_merge_destroy(&result);
    return false;
}
