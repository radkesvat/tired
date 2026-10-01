#include "tired/encode.h"
#include "tired/file_target.h"
#include "tired/io.h"
#include "tired/json.h"
#include "tired/mutation.h"
#include "tired/render.h"
#include "tired/sha256.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

void tired_mutation_destroy(TiredMutation *mutation)
{
    if (mutation == NULL)
        return;
    tired_service_record_destroy(&mutation->proposed);
    tired_text_destroy(&mutation->previous_name);
    tired_text_destroy(&mutation->expected_unit_sha256);
    tired_text_destroy(&mutation->expected_record_sha256);
    *mutation = (TiredMutation){0};
}
void tired_operation_result_destroy(TiredOperationResult *result)
{
    if (result == NULL)
        return;
    tired_text_destroy(&result->unit_name);
    tired_text_destroy(&result->unit_path);
    tired_text_destroy(&result->run_as);
    tired_runtime_destroy(&result->runtime);
    *result = (TiredOperationResult){0};
}
void tired_mutation_change(TiredMutation *mutation, TiredFieldId field)
{
    bool *acknowledged = mutation->proposed.review.acknowledged;
    if (field == TIRED_FIELD_RUN_AS)
        acknowledged[TIRED_RISK_ROOT] = false;
    if (field == TIRED_FIELD_AMBIENT_CAPABILITIES)
        acknowledged[TIRED_RISK_CAPABILITIES] = false;
    if (field == TIRED_FIELD_EXECUTABLE || field == TIRED_FIELD_ARGV ||
        field == TIRED_FIELD_AMBIENT_CAPABILITIES || field == TIRED_FIELD_RUN_AS)
        acknowledged[TIRED_RISK_WRITABLE_CODE] = false;
    if (field == TIRED_FIELD_ARGV || field == TIRED_FIELD_EXECUTABLE)
        acknowledged[TIRED_RISK_SENSITIVE_COMMAND] = false;
    if (field == TIRED_FIELD_RESTART_SEC || field == TIRED_FIELD_RETRY_POLICY)
        acknowledged[TIRED_RISK_RAPID_RETRY] = false;
    if (field == TIRED_FIELD_KILL_MODE)
        acknowledged[TIRED_RISK_CHILD_PROCESSES] = false;
}
bool tired_digest_bytes(const char *bytes, size_t length, char digest[65], TiredError *error)
{
    tired_sha256(bytes, length, digest);
    tired_error_clear(error);
    return true;
}
static bool put(struct json_object *object, const char *key, struct json_object *value)
{
    if (value == NULL)
        return false;
    if (json_object_object_add(object, key, value) == 0)
        return true;
    json_object_put(value);
    return false;
}
static bool text(TiredText *output, const char *input, TiredError *error)
{
    return tired_text_set(output, input, strlen(input), TIRED_INPUT_LIMIT, error);
}
static bool location(const TiredLayout *layout, TiredServiceRecord *record,
                     TiredFileTargetRole role, TiredText *output, TiredError *error)
{
    TiredFileTarget target = {.role = role};
    memcpy(target.service_uuid, record->metadata.service_uuid, 37);
    if (role == TIRED_FILE_TARGET_UNIT)
        target.unit_name = record->metadata.unit_name;
    else if (role == TIRED_FILE_TARGET_ENVIRONMENT)
        memcpy(target.revision_uuid, record->environment_revision, 37);
    TiredResolvedFile resolved = {0};
    bool ok = tired_file_target_resolve(layout, &target, &resolved, error) &&
              tired_path_absolute(&resolved.directory, resolved.name.data, resolved.name.length,
                                  output, error);
    tired_resolved_file_destroy(&resolved);
    return ok;
}
bool tired_mutation_from_plan(const TiredPlan *plan, const TiredLayout *layout,
                              const TiredSettings *settings, const TiredTextList *risks,
                              TiredMutation *output, TiredError *error)
{
    TiredMutation mutation = {.operation = TIRED_TRANSACTION_CREATE,
                              .actor_uid = plan->invoking.uid,
                              .observation_usec = settings->observation_usec,
                              .history_revisions = settings->history_revisions};
    TiredServiceRecord *record = &mutation.proposed;
    record->environment_loaded = true;
    TiredText bytes = {0}, environment = {0}, unit = {0};
    bool ok = false;
    memcpy(record->metadata.service_uuid, plan->uuid, 37);
    if (!tired_uuid_create(record->metadata.revision_uuid, error) ||
        !tired_uuid_create(record->metadata.transaction_uuid, error))
        goto done;
    const TiredText *name = &plan->spec.fields[TIRED_FIELD_NAME].value.text;
    TiredBuffer full;
    tired_buffer_init(&full, 255);
    bool built = tired_buffer_append(&full, name->data, name->length, error) &&
                 tired_buffer_append(&full, ".service", 8, error) &&
                 tired_buffer_take(&full, &record->metadata.unit_name, error);
    tired_buffer_destroy(&full);
    if (!built || !text(&record->metadata.writer_version, "0.1.0", error))
        goto done;
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        goto done;
    record->metadata.created_usec = record->metadata.updated_usec =
        (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
    record->metadata.user_scope = layout->user_scope;
    record->metadata.owner_uid = layout->user_scope ? plan->invoking.uid : 0;
    record->metadata.invoking_uid = plan->invoking.uid;
    record->metadata.invoking_gid = plan->invocation.gid;
    record->metadata.service_uid = plan->service.uid;
    record->metadata.service_gid = plan->group.gid;
    if (!tired_spec_encode(&plan->spec, &bytes, error) ||
        !tired_spec_parse(bytes.data, bytes.length, &record->spec, error) ||
        !tired_environment_snapshot_encode(&plan->environment, &plan->credentials, &bytes, error) ||
        !tired_environment_snapshot_parse(bytes.data, bytes.length, &record->environment,
                                          &record->credentials, error) ||
        !tired_executable_evidence_capture(&plan->invocation, &record->executable, error) ||
        !location(layout, record, TIRED_FILE_TARGET_UNIT, &record->unit_path, error))
        goto done;
    record->linger_requested = record->spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean;
    record->review.argument_count = plan->invocation.argv.count;
    memcpy(record->review.sensitive_arguments, plan->sensitive_arguments,
           sizeof(record->review.sensitive_arguments));
    memset(record->review.approved_sha256, '0', 64);
    record->review.approved_sha256[64] = '\0';
    for (size_t i = 0; i < risks->count; ++i)
    {
        TiredRiskId id;
        if (!tired_risk_find(risks->items[i].data, risks->items[i].length, &id))
            goto done;
        record->review.acknowledged[id] = true;
    }
    if (plan->environment.count != 0)
    {
        record->has_environment = true;
        memcpy(record->environment_revision, record->metadata.revision_uuid, 37);
        if (!location(layout, record, TIRED_FILE_TARGET_ENVIRONMENT, &record->environment_path,
                      error) ||
            !tired_environment_encode(&record->environment, &environment, error) ||
            !tired_digest_bytes(environment.data, environment.length, record->environment_sha256,
                                error))
            goto done;
    }
    if (plan->profile.document != NULL)
    {
        TiredProfileSnapshot snapshot = {.profile = plan->profile,
                                         .source_path = plan->profile_path,
                                         .source_origin = plan->profile_origin,
                                         .explicit_selection = plan->profile_explicit,
                                         .decisions = plan->profile_decisions,
                                         .count = plan->profile.count};
        memcpy(snapshot.source_sha256, plan->profile_digest, 65);
        if (!tired_profile_snapshot_encode(&snapshot, &bytes, error) ||
            !tired_profile_snapshot_parse(bytes.data, bytes.length, &record->profile, error))
            goto done;
        record->has_profile = true;
    }
    if (!tired_render_unit(&record->spec, record->metadata.service_uuid,
                           record->has_environment ? &record->environment_path : NULL,
                           &record->credentials, &unit, error) ||
        !tired_digest_bytes(unit.data, unit.length, record->metadata.unit_sha256, error) ||
        !tired_mutation_digest(&mutation, record->review.approved_sha256, error))
        goto done;
    tired_mutation_destroy(output);
    *output = mutation;
    mutation = (TiredMutation){0};
    ok = true;
done:
    tired_text_destroy(&bytes);
    tired_text_destroy(&environment);
    tired_text_destroy(&unit);
    tired_mutation_destroy(&mutation);
    return ok;
}
bool tired_mutation_encode(const TiredMutation *mutation, TiredText *output, TiredError *error)
{
    struct json_object *document = json_object_new_object(), *record = NULL, *values = NULL;
    TiredText bytes = {0};
    bool ok = false;
    if (document == NULL || !tired_service_record_encode(&mutation->proposed, &bytes, error) ||
        !tired_json_parse(bytes.data, bytes.length, TIRED_INPUT_LIMIT, &record, error))
        goto done;
    bool inserted = put(document, "proposal", record);
    record = NULL;
    if (mutation->proposed.environment_loaded)
    {
        if (!tired_environment_snapshot_encode(&mutation->proposed.environment,
                                               &mutation->proposed.credentials, &bytes, error) ||
            !tired_json_parse(bytes.data, bytes.length, TIRED_INPUT_LIMIT, &values, error))
            goto done;
    }
    bool values_inserted = json_object_object_add(document, "values", values) == 0;
    if (values_inserted)
        values = NULL;
    if (!inserted || !values_inserted ||
        !put(document, "protocol_version", json_object_new_int(1)) ||
        !put(document, "actor_uid", json_object_new_uint64(mutation->actor_uid)) ||
        !put(document, "operation", json_object_new_int(mutation->operation)) ||
        !put(document, "previous_name",
             json_object_new_string(
                 mutation->previous_name.data == NULL ? "" : mutation->previous_name.data)) ||
        !put(document, "expected_unit_sha256",
             json_object_new_string(mutation->expected_unit_sha256.data == NULL
                                        ? ""
                                        : mutation->expected_unit_sha256.data)) ||
        !put(document, "expected_record_sha256",
             json_object_new_string(mutation->expected_record_sha256.data == NULL
                                        ? ""
                                        : mutation->expected_record_sha256.data)) ||
        !put(document, "now", json_object_new_boolean(mutation->now)) ||
        !put(document, "defer", json_object_new_boolean(mutation->defer)) ||
        !put(document, "restore_drift", json_object_new_boolean(mutation->restore_drift)) ||
        !put(document, "keep_history", json_object_new_boolean(mutation->keep_history)) ||
        !put(document, "recovery", json_object_new_boolean(mutation->recovery)) ||
        !put(document, "finish", json_object_new_boolean(mutation->finish)) ||
        !put(document, "observation_usec", json_object_new_uint64(mutation->observation_usec)) ||
        !put(document, "history_revisions", json_object_new_uint64(mutation->history_revisions)))
        goto allocation;
    const char *encoded = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    if (encoded == NULL)
        goto allocation;
    ok = tired_text_set(output, encoded, strlen(encoded), TIRED_INPUT_LIMIT, error);
    goto done;
allocation:
    tired_error_set(error, TIRED_INTERNAL, "allocation", "Cannot encode helper request.", 0);
done:
    json_object_put(document);
    json_object_put(record);
    json_object_put(values);
    tired_text_destroy(&bytes);
    return ok;
}
bool tired_mutation_digest(const TiredMutation *mutation, char digest[65], TiredError *error)
{
    TiredMutation view = *mutation;
    view.proposed.environment_loaded = false;
    memset(view.proposed.review.approved_sha256, '0', 64);
    view.proposed.review.approved_sha256[64] = '\0';
    TiredText bytes = {0};
    struct json_object *document = NULL, *proposal = NULL;
    bool ok = tired_mutation_encode(&view, &bytes, error) &&
              tired_json_parse(bytes.data, bytes.length, TIRED_INPUT_LIMIT, &document, error) &&
              json_object_object_get_ex(document, "proposal", &proposal);
    if (ok && view.actor_uid == view.proposed.metadata.invoking_uid)
        json_object_object_del(document, "actor_uid");
    if (ok && view.proposed.former_unit_names.count == 0)
        json_object_object_del(proposal, "former_unit_names");
    if (ok && view.proposed.linger_requested ==
                  view.proposed.spec.fields[TIRED_FIELD_ENABLE_LINGER].value.boolean)
        json_object_object_del(proposal, "linger_requested");
    const char *canonical =
        ok ? json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN) : NULL;
    ok = canonical != NULL && tired_digest_bytes(canonical, strlen(canonical), digest, error);
    json_object_put(document);
    tired_text_destroy(&bytes);
    return ok;
}
static bool read_string(struct json_object *document, const char *key, TiredText *output,
                        TiredError *error)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(document, key, &value) ||
        !json_object_is_type(value, json_type_string))
        return false;
    return tired_text_set(output, json_object_get_string(value),
                          (size_t)json_object_get_string_len(value), 255, error);
}
static bool read_bool(struct json_object *document, const char *key, bool *output)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(document, key, &value) ||
        !json_object_is_type(value, json_type_boolean))
        return false;
    *output = json_object_get_boolean(value);
    return true;
}
bool tired_mutation_parse(const char *bytes, size_t length, TiredMutation *output,
                          TiredError *error)
{
    struct json_object *document = NULL, *value = NULL;
    TiredMutation mutation = {0};
    uint64_t number;
    bool ok = false;
    if (!tired_json_parse(bytes, length, TIRED_INPUT_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        (json_object_object_length(document) != 15 && json_object_object_length(document) != 16) ||
        !json_object_object_get_ex(document, "protocol_version", &value) ||
        !tired_json_u64(value, 1, 1, &number, error) ||
        !json_object_object_get_ex(document, "operation", &value) ||
        !tired_json_u64(value, 0, TIRED_TRANSACTION_OPERATION_COUNT - 1, &number, error))
        goto invalid;
    mutation.operation = (TiredTransactionOperation)number;
    if (!json_object_object_get_ex(document, "proposal", &value) ||
        !json_object_is_type(value, json_type_object))
        goto invalid;
    const char *encoded = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    if (encoded == NULL ||
        !tired_service_record_parse(encoded, strlen(encoded), &mutation.proposed, error) ||
        !read_string(document, "previous_name", &mutation.previous_name, error) ||
        !read_string(document, "expected_unit_sha256", &mutation.expected_unit_sha256, error) ||
        !read_string(document, "expected_record_sha256", &mutation.expected_record_sha256, error) ||
        !read_bool(document, "now", &mutation.now) ||
        !read_bool(document, "defer", &mutation.defer) ||
        !read_bool(document, "restore_drift", &mutation.restore_drift) ||
        !read_bool(document, "keep_history", &mutation.keep_history) ||
        !read_bool(document, "recovery", &mutation.recovery) ||
        !read_bool(document, "finish", &mutation.finish) ||
        !json_object_object_get_ex(document, "observation_usec", &value) ||
        !tired_json_u64(value, 0, 30000000, &mutation.observation_usec, error) ||
        !json_object_object_get_ex(document, "history_revisions", &value) ||
        !tired_json_u64(value, 0, 1000, &mutation.history_revisions, error))
        goto invalid;
    if (json_object_object_get_ex(document, "actor_uid", &value))
    {
        if (json_object_object_length(document) != 16 ||
            !tired_json_u64(value, 0, UINT32_MAX - 1, &number, error))
            goto invalid;
        mutation.actor_uid = (uid_t)number;
    }
    else
    {
        if (json_object_object_length(document) != 15)
            goto invalid;
        mutation.actor_uid = mutation.proposed.metadata.invoking_uid;
    }
    if (!json_object_object_get_ex(document, "values", &value))
        goto invalid;
    if (value != NULL)
    {
        const char *assignments = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
        TiredEnvironment values_environment = {0};
        TiredCredentials values_credentials = {0};
        bool consistent =
            assignments != NULL &&
            tired_environment_snapshot_parse(assignments, strlen(assignments), &values_environment,
                                             &values_credentials, error) &&
            values_environment.count == mutation.proposed.environment.count &&
            values_credentials.count == mutation.proposed.credentials.count;
        for (size_t i = 0; consistent && i < values_environment.count; ++i)
        {
            const TiredEnvironmentEntry *entry = &values_environment.items[i];
            const TiredEnvironmentEntry *metadata = tired_environment_find(
                &mutation.proposed.environment, entry->name.data, entry->name.length);
            consistent = metadata != NULL && metadata->origin == entry->origin &&
                         metadata->sensitive == entry->sensitive;
        }
        for (size_t i = 0; consistent && i < values_credentials.count; ++i)
            consistent = strcmp(values_credentials.items[i].name.data,
                                mutation.proposed.credentials.items[i].name.data) == 0 &&
                         strcmp(values_credentials.items[i].path.data,
                                mutation.proposed.credentials.items[i].path.data) == 0;
        if (consistent && mutation.proposed.has_environment)
        {
            TiredText contents = {0};
            char digest[65];
            consistent = tired_environment_encode(&values_environment, &contents, error) &&
                         tired_digest_bytes(contents.data, contents.length, digest, error) &&
                         strcmp(digest, mutation.proposed.environment_sha256) == 0;
            tired_text_destroy(&contents);
        }
        if (consistent)
        {
            tired_environment_destroy(&mutation.proposed.environment);
            mutation.proposed.environment = values_environment;
            values_environment = (TiredEnvironment){0};
            mutation.proposed.environment_loaded = true;
        }
        tired_environment_destroy(&values_environment);
        tired_credentials_destroy(&values_credentials);
        if (!consistent)
            goto invalid;
    }
    char digest[65];
    if (!tired_mutation_digest(&mutation, digest, error) ||
        strcmp(digest, mutation.proposed.review.approved_sha256) != 0)
        goto invalid;
    tired_mutation_destroy(output);
    *output = mutation;
    mutation = (TiredMutation){0};
    ok = true;
    goto done;
invalid:
    if (error->status == TIRED_OK)
        tired_error_set(error, TIRED_INVALID, "helper-protocol",
                        "Invalid, incompatible or altered administrative request.", 0);
done:
    json_object_put(document);
    tired_mutation_destroy(&mutation);
    return ok;
}
