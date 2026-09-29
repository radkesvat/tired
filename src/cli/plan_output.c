#include "tired/plan_output.h"
#include "tired/encode.h"
#include "tired/render.h"
#include "tired/risk.h"
#include <assert.h>
#include <json-c/json.h>
#include <string.h>

static bool secret_flag(const TiredText *arg)
{
    if (arg->length == 0 || arg->data[0] != '-')
        return false;
    char key[128];
    size_t i = 0;
    for (; i < arg->length && i < sizeof(key) - 1 && arg->data[i] != '='; ++i)
    {
        char c = arg->data[i];
        key[i] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
    }
    key[i] = '\0';
    return strstr(key, "password") != NULL || strstr(key, "passwd") != NULL ||
           strstr(key, "token") != NULL || strstr(key, "secret") != NULL ||
           strstr(key, "api-key") != NULL;
}
static bool add(struct json_object *object, const char *key, struct json_object *child)
{
    if (child == NULL)
        return false;
    if (json_object_object_add(object, key, child) != 0)
    {
        json_object_put(child);
        return false;
    }
    return true;
}
static bool push(struct json_object *array, struct json_object *child)
{
    if (child == NULL)
        return false;
    if (json_object_array_add(array, child) != 0)
    {
        json_object_put(child);
        return false;
    }
    return true;
}

static bool comment(TiredBuffer *buffer, const char *label, const char *value, TiredError *error)
{
    TiredText safe = {0};
    bool ok = tired_encode_display(value, strlen(value), &safe, error) &&
              tired_buffer_append(buffer, "# ", 2, error) &&
              tired_buffer_append(buffer, label, strlen(label), error) &&
              tired_buffer_append(buffer, ": ", 2, error) &&
              tired_buffer_append(buffer, safe.data, safe.length, error) &&
              tired_buffer_append(buffer, "\n", 1, error);
    tired_text_destroy(&safe);
    return ok;
}
static struct json_object *field_value(const TiredField *field, const TiredFieldValue *value)
{
    switch (field->kind)
    {
    case TIRED_FIELD_TEXT:
        return json_object_new_string_len(value->value.text.data, (int)value->value.text.length);
    case TIRED_FIELD_CHOICE:
    {
        const char *start = field->choices;
        for (size_t i = 0; i < value->value.choice; ++i)
        {
            start = strchr(start, '|');
            if (start == NULL)
                return NULL;
            ++start;
        }
        const char *end = strchr(start, '|');
        return json_object_new_string_len(
            start, (int)(end == NULL ? strlen(start) : (size_t)(end - start)));
    }
    case TIRED_FIELD_BOOL:
        return json_object_new_boolean(value->value.boolean);
    case TIRED_FIELD_INTEGER:
        return json_object_new_int64(value->value.integer);
    case TIRED_FIELD_DURATION:
        return json_object_new_uint64(value->value.microseconds);
    case TIRED_FIELD_TIMEOUT:
        return value->value.timeout.infinity ? json_object_new_string("infinity")
                                             : json_object_new_uint64(value->value.timeout.value);
    case TIRED_FIELD_MODE:
        return json_object_new_int64(value->value.mode);
    case TIRED_FIELD_SIGNAL:
        return json_object_new_int(value->value.signal_number);
    case TIRED_FIELD_QUOTA:
        return json_object_new_uint64(value->value.quota);
    case TIRED_FIELD_LIMIT:
        return value->value.limit.infinity ? json_object_new_string("infinity")
                                           : json_object_new_uint64(value->value.limit.value);
    case TIRED_FIELD_LIST:
    {
        struct json_object *array = json_object_new_array();
        if (array == NULL)
            return NULL;
        for (size_t i = 0; i < value->value.list.count; ++i)
            if (!push(array, json_object_new_string_len(value->value.list.items[i].data,
                                                        (int)value->value.list.items[i].length)))
            {
                json_object_put(array);
                return NULL;
            }
        return array;
    }
    }
    return NULL;
}

static const char *field_origins[] = {"unset",       "inherited", "default", "administrator-config",
                                      "user-config", "profile",   "user",    "capture"};

static bool output_plan(const TiredPlan *plan, bool json, bool unit_only, bool include_sensitive,
                        bool explain, TiredText *output, TiredError *error)
{
    assert(plan != NULL && output != NULL);
    TiredServiceSpec display = {0};
    TiredText unit = {0};
    TiredBuffer text;
    tired_buffer_init(&text, TIRED_UNIT_LIMIT * 4U);
    struct json_object *root = NULL, *fields = NULL, *environment = NULL, *warnings = NULL;
    bool redacted = false, sensitive = false;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        if (!tired_spec_copy_field(&display, &plan->spec, (TiredFieldId)i, error))
            goto fail;
    const TiredTextList *original = &plan->spec.fields[TIRED_FIELD_ARGV].value.list;
    if (!tired_spec_clear_list(&display, TIRED_FIELD_ARGV, TIRED_ORIGIN_CAPTURE, error))
        goto fail;
    bool hide_next = false;
    for (size_t i = 0; i < original->count; ++i)
    {
        const TiredText *arg = &original->items[i];
        bool flag = i != 0 && secret_flag(arg);
        bool attached = flag && memchr(arg->data, '=', arg->length) != NULL;
        bool hide = hide_next || attached;
        if (hide || flag)
            sensitive = true;
        const char *value = !include_sensitive && hide ? "[redacted]" : arg->data;
        size_t length = !include_sensitive && hide ? sizeof("[redacted]") - 1 : arg->length;
        if (!include_sensitive && hide)
            redacted = true;
        if (!tired_spec_append(&display, TIRED_FIELD_ARGV, value, length, TIRED_ORIGIN_CAPTURE,
                               error))
            goto fail;
        hide_next = flag && !attached;
    }
    if (!tired_render_unit(&display, plan->uuid,
                           plan->managed_environment.data == NULL ? NULL
                                                                  : &plan->managed_environment,
                           &plan->credentials, &unit, error))
        goto fail;
    TiredRiskFacts facts = {.invoking_uid = plan->invoking.uid,
                            .service_uid = plan->service.uid,
                            .sensitive_command = sensitive,
                            .sensitive_export = include_sensitive};
    TiredRiskReport risks;
    tired_risk_assess(&plan->spec, &facts, &risks);
    if (json)
    {
        root = json_object_new_object();
        fields = json_object_new_object();
        environment = json_object_new_array();
        warnings = json_object_new_array();
        if (root == NULL || fields == NULL || environment == NULL || warnings == NULL)
            goto allocation;
        for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        {
            const TiredField *field = tired_field_get((TiredFieldId)i);
            const TiredFieldValue *value = &display.fields[i];
            struct json_object *item = json_object_new_object();
            if (item == NULL)
                goto allocation;
            if (!add(item, "origin", json_object_new_string(field_origins[value->origin])) ||
                !add(item, "inherit", json_object_new_boolean(value->inherit)) ||
                (tired_field_has_value(value) && !add(item, "value", field_value(field, value))))
            {
                json_object_put(item);
                goto allocation;
            }
            if (!add(fields, field->name, item))
                goto allocation;
        }
        for (size_t i = 0; i < plan->environment.count; ++i)
        {
            const TiredEnvironmentEntry *entry = &plan->environment.items[i];
            struct json_object *item = json_object_new_object();
            if (item == NULL)
                goto allocation;
            if (!add(item, "name", json_object_new_string(entry->name.data)) ||
                !add(item, "value",
                     json_object_new_string(include_sensitive
                                                ? entry->value.data
                                                : tired_environment_display(entry))) ||
                !add(item, "sensitive", json_object_new_boolean(entry->sensitive)) ||
                !add(item, "origin", json_object_new_int(entry->origin)))
            {
                json_object_put(item);
                goto allocation;
            }
            if (!push(environment, item))
                goto allocation;
        }
        struct json_object *checks = json_object_new_array();
        if (checks == NULL)
            goto allocation;
        if (!add(root, "risk_checks", checks))
            goto allocation;
        for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
        {
            if (!risks.present[i] && !risks.pending[i])
                continue;
            const TiredRisk *risk = tired_risk_get((TiredRiskId)i);
            if (risks.present[i] && !push(warnings, json_object_new_string(risk->code)))
                goto allocation;
            struct json_object *entry = json_object_new_object();
            if (entry == NULL)
                goto allocation;
            if (!add(entry, "code", json_object_new_string(risk->code)) ||
                !add(entry, "state",
                     json_object_new_string(risks.pending[i] ? "inspection-pending"
                                                             : "acknowledgment-required")) ||
                !add(entry, "message",
                     json_object_new_string(
                         risks.pending[i]
                             ? "Privileged code ownership and writability have not been inspected."
                             : risk->message)))
            {
                json_object_put(entry);
                goto allocation;
            }
            if (!push(checks, entry))
                goto allocation;
        }
        if (!add(root, "schema_version", json_object_new_int(1)) ||
            !add(root, "command", json_object_new_string(explain ? "profiles explain" : "plan")) ||
            !add(root, "ok", json_object_new_boolean(true)) ||
            !add(root, "exit_code", json_object_new_int(0)) ||
            !add(root, "live_validation", json_object_new_string("not_performed")) ||
            !add(root, "collision_check", json_object_new_string("not_performed")) ||
            !add(root, "replayable", json_object_new_boolean(false)) ||
            !add(root, "profile",
                 json_object_new_string(plan->profile.id == NULL ? "generic" : plan->profile.id)) ||
            !add(root, "profile_matching",
                 json_object_new_string(!plan->profile_matching              ? "disabled"
                                        : plan->profile_candidates.count > 1 ? "ambiguous"
                                        : plan->profile_explicit             ? "explicit"
                                        : plan->profile.id == NULL ||
                                                strcmp(plan->profile.id, "generic") == 0
                                            ? "generic-fallback"
                                            : "executable-name")) ||
            !add(root, "target_systemd_min", json_object_new_int(249)) ||
            !add(root, "unit_redacted", json_object_new_boolean(redacted)) ||
            !add(root, "unit", json_object_new_string_len(unit.data, (int)unit.length)))
            goto allocation;
        struct json_object *candidates = json_object_new_array();
        if (candidates == NULL)
            goto allocation;
        for (size_t i = 0; i < plan->profile_candidates.count; ++i)
            if (!push(candidates, json_object_new_string(plan->profile_candidates.items[i].data)))
            {
                json_object_put(candidates);
                goto allocation;
            }
        if (!add(root, "profile_candidates", candidates))
            goto allocation;
        if (plan->profile.document != NULL)
        {
            if (!add(root, "profile_snapshot", json_object_get(plan->profile.document)) ||
                !add(root, "profile_digest", json_object_new_string(plan->profile_digest)) ||
                !add(root, "profile_source", json_object_new_string(plan->profile_path.data)) ||
                !add(root, "profile_origin",
                     json_object_new_string(
                         plan->profile_origin == TIRED_PROFILE_BUNDLED ? "bundled"
                         : plan->profile_origin == TIRED_PROFILE_ADMIN ? "administrator"
                                                                       : "user")))
                goto allocation;
            struct json_object *decisions = json_object_new_array();
            if (decisions == NULL)
                goto allocation;
            bool ok = true;
            for (size_t i = 0; ok && i < plan->profile.count; ++i)
            {
                struct json_object *entry = json_object_new_object();
                if (entry == NULL)
                {
                    ok = false;
                    break;
                }
                if (!add(entry, "field",
                         json_object_new_string(
                             tired_field_get(plan->profile.recommendations[i].field)->name)) ||
                    !add(entry, "disposition",
                         json_object_new_string(
                             tired_recommendation_disposition_name(plan->profile_decisions[i]))) ||
                    !add(entry, "reason",
                         json_object_new_string(plan->profile.recommendations[i].reason)))
                {
                    json_object_put(entry);
                    ok = false;
                    break;
                }
                ok = push(decisions, entry);
            }
            if (!ok)
            {
                json_object_put(decisions);
                goto allocation;
            }
            if (!add(root, "recommendations", decisions))
                goto allocation;
        }
        if (plan->profile_candidates.count > 1 &&
            !push(warnings, json_object_new_string("ambiguous-profile")))
            goto allocation;
        for (size_t i = 0; i < plan->profile.count; ++i)
            if ((plan->profile_decisions[i] == TIRED_RECOMMENDATION_CONFLICT ||
                 plan->profile_decisions[i] == TIRED_RECOMMENDATION_REQUIRED_CONFLICT) &&
                !push(warnings, json_object_new_string(tired_recommendation_disposition_name(
                                    plan->profile_decisions[i]))))
                goto allocation;
        bool added = add(root, "fields", fields);
        fields = NULL;
        if (!added)
            goto allocation;
        added = add(root, "environment", environment);
        environment = NULL;
        if (!added)
            goto allocation;
        added = add(root, "warnings", warnings);
        warnings = NULL;
        if (!added)
            goto allocation;
        const char *encoded = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
        if (encoded == NULL)
            goto allocation;
        for (size_t i = 0; encoded[i] != '\0'; ++i)
        {
            if ((unsigned char)encoded[i] == 0xc2 && (unsigned char)encoded[i + 1] >= 0x80 &&
                (unsigned char)encoded[i + 1] <= 0x9f)
            {
                static const char hex[] = "0123456789abcdef";
                unsigned char c = (unsigned char)encoded[++i];
                char escape[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                if (!tired_buffer_append(&text, escape, sizeof(escape), error))
                    goto fail;
            }
            else if (!tired_buffer_append(&text, encoded + i, 1, error))
                goto fail;
        }
        if (!tired_buffer_append(&text, "\n", 1, error))
            goto fail;
    }
    else
    {
        const char *heading =
            redacted ? "# Redacted non-installable view; live validation not performed\n"
                     : "# Offline proposal; live validation and collision checks not performed\n";
        if (!tired_buffer_append(&text, heading, strlen(heading), error))
            goto fail;
        if (explain && !comment(&text, "Profile explanation",
                                "Passive evaluation; target not executed", error))
            goto fail;
        if (!unit_only)
        {
            for (unsigned i = 0; i < TIRED_RISK_COUNT; ++i)
            {
                if (!risks.present[i] && !risks.pending[i])
                    continue;
                const TiredRisk *risk = tired_risk_get((TiredRiskId)i);
                if (!comment(&text, risk->code,
                             risks.pending[i]
                                 ? "Inspection pending: privileged code ownership and writability."
                                 : risk->message,
                             error))
                    goto fail;
            }
            if (!comment(&text, "Retry policy origin",
                         field_origins[plan->spec.fields[TIRED_FIELD_RETRY_POLICY].origin],
                         error) ||
                !comment(&text, "Restart delay origin",
                         field_origins[plan->spec.fields[TIRED_FIELD_RESTART_SEC].origin], error))
                goto fail;
            if (!comment(&text, "Profile",
                         plan->profile.name == NULL ? "Generic; application requirements unknown"
                                                    : plan->profile.name,
                         error))
                goto fail;
            if (!plan->profile_matching &&
                !comment(&text, "Match", "Profile matching disabled", error))
                goto fail;
            if (plan->profile.document != NULL)
            {
                if (!comment(&text, "Match",
                             plan->profile_explicit
                                 ? "Explicit selection; binary identity is not verified"
                             : strcmp(plan->profile.id, "generic") == 0
                                 ? "Generic fallback"
                                 : "Executable name; binary identity is not verified",
                             error) ||
                    !comment(&text, "Profile source", plan->profile_path.data, error) ||
                    !comment(&text, "Profile digest", plan->profile_digest, error))
                    goto fail;
                for (size_t i = 0; i < plan->profile.count; ++i)
                    if (!comment(&text,
                                 tired_field_get(plan->profile.recommendations[i].field)->name,
                                 tired_recommendation_disposition_name(plan->profile_decisions[i]),
                                 error) ||
                        !comment(&text, "Reason", plan->profile.recommendations[i].reason, error))
                        goto fail;
                struct json_object *sources = NULL, *advisories = NULL;
                (void)json_object_object_get_ex(plan->profile.document, "sources", &sources);
                (void)json_object_object_get_ex(plan->profile.document, "advisories", &advisories);
                for (size_t i = 0; i < json_object_array_length(sources); ++i)
                {
                    struct json_object *url = NULL;
                    (void)json_object_object_get_ex(json_object_array_get_idx(sources, i), "url",
                                                    &url);
                    if (!comment(&text, "Evidence", json_object_get_string(url), error))
                        goto fail;
                }
                for (size_t i = 0; i < json_object_array_length(advisories); ++i)
                {
                    struct json_object *message = NULL;
                    (void)json_object_object_get_ex(json_object_array_get_idx(advisories, i),
                                                    "message", &message);
                    if (!comment(&text, "Advisory", json_object_get_string(message), error))
                        goto fail;
                }
            }
            if (plan->profile_candidates.count > 1)
            {
                if (!comment(&text, "Warning",
                             "Ambiguous profile matches; generic settings retained", error))
                    goto fail;
                for (size_t i = 0; i < plan->profile_candidates.count; ++i)
                    if (!comment(&text, "Candidate", plan->profile_candidates.items[i].data, error))
                        goto fail;
            }
        }
        if (sensitive &&
            !tired_buffer_append(
                &text, "# Warning: sensitive-command-data; actual unit retains command secrets\n",
                sizeof("# Warning: sensitive-command-data; actual unit retains command secrets\n") -
                    1,
                error))
            goto fail;
        /* Token/scalar encoders prevent raw terminal control sequences. */
        if (!tired_buffer_append(&text, unit.data, unit.length, error))
            goto fail;
    }
    if (!tired_buffer_take(&text, output, error))
        goto fail;
    tired_spec_destroy(&display);
    tired_text_destroy(&unit);
    json_object_put(root);
    json_object_put(fields);
    json_object_put(environment);
    json_object_put(warnings);
    return true;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot construct plan output.", 0);
fail:
    tired_spec_destroy(&display);
    tired_text_destroy(&unit);
    tired_buffer_destroy(&text);
    json_object_put(root);
    json_object_put(fields);
    json_object_put(environment);
    json_object_put(warnings);
    return false;
}

bool tired_plan_output(const TiredPlan *plan, bool json, bool unit_only, bool include_sensitive,
                       TiredText *output, TiredError *error)
{
    return output_plan(plan, json, unit_only, include_sensitive, false, output, error);
}

bool tired_plan_explain_output(const TiredPlan *plan, bool json, TiredText *output,
                               TiredError *error)
{
    return output_plan(plan, json, false, false, true, output, error);
}
