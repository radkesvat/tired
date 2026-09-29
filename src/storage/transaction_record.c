#include "tired/transaction_record.h"
#include "tired/json.h"
#include "tired/name.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *const operations[] = {"create", "edit", "remove",  "rename", "restore",
                                         "start",  "stop", "restart", "enable", "disable"};
static const char *const actions[] = {
    "prepare", "publish_files", "reload",       "enable",       "disable",  "start", "stop",
    "restart", "observe",       "store_record", "remove_files", "rollback", "commit"};
static const char *const states[] = {"intent", "completed", "failed", "uncertain"};
_Static_assert(sizeof(operations) / sizeof(operations[0]) == TIRED_TRANSACTION_OPERATION_COUNT,
               "Operation names");
_Static_assert(sizeof(actions) / sizeof(actions[0]) == TIRED_ACTION_COUNT, "Action names");
_Static_assert(sizeof(states) / sizeof(states[0]) == TIRED_ACTION_STATE_COUNT, "State names");
static bool invalid(TiredError *error)
{
    return tired_error_set(
        error, TIRED_INVALID, "transaction-record-schema",
        "Transaction progress record contains missing, unknown or invalid fields.", 0);
}
static bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
static bool uuid(const char value[37])
{
    if (strnlen(value, 37) != 36 || value[14] != '4' ||
        (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b'))
        return false;
    for (size_t i = 0; i < 36; ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (value[i] != '-')
                return false;
        }
        else if (!hex(value[i]))
            return false;
    return true;
}
static bool validate(const TiredTransactionRecord *record, TiredError *error)
{
    if (!uuid(record->transaction_uuid) || !uuid(record->service_uuid) ||
        strnlen(record->approved_sha256, 65) != 64 || record->sequence == 0 ||
        record->sequence > TIRED_TRANSACTION_SEQUENCE_LIMIT ||
        (unsigned)record->operation >= TIRED_TRANSACTION_OPERATION_COUNT ||
        (unsigned)record->action >= TIRED_ACTION_COUNT ||
        (unsigned)record->state >= TIRED_ACTION_STATE_COUNT)
        return invalid(error);
    for (size_t i = 0; i < 64; ++i)
        if (!hex(record->approved_sha256[i]))
            return invalid(error);
    const TiredText *name = &record->unit_name;
    if (name->data == NULL || name->length <= 8 || name->length > TIRED_EXPLICIT_NAME_LIMIT + 8 ||
        memcmp(name->data + name->length - 8, ".service", 8) != 0 ||
        !tired_name_validate_base(name->data, name->length - 8, error))
        return invalid(error);
    return true;
}
void tired_transaction_record_destroy(TiredTransactionRecord *record)
{
    if (record == NULL)
        return;
    tired_text_destroy(&record->unit_name);
    *record = (TiredTransactionRecord){0};
}
static const char *string_field(struct json_object *document, const char *key, size_t *length)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(document, key, &value) ||
        !json_object_is_type(value, json_type_string))
        return NULL;
    *length = (size_t)json_object_get_string_len(value);
    return json_object_get_string(value);
}
static bool fixed_field(struct json_object *document, const char *key, char *output, size_t length)
{
    size_t size = 0;
    const char *value = string_field(document, key, &size);
    if (value == NULL || size != length)
        return false;
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}
static int choice(struct json_object *document, const char *key, const char *const *names,
                  size_t count)
{
    size_t length = 0;
    const char *value = string_field(document, key, &length);
    if (value == NULL)
        return -1;
    for (size_t i = 0; i < count; ++i)
        if (strlen(names[i]) == length && memcmp(names[i], value, length) == 0)
            return (int)i;
    return -1;
}
bool tired_transaction_record_parse(const char *data, size_t length, TiredTransactionRecord *output,
                                    TiredError *error)
{
    assert(output != NULL);
    struct json_object *document = NULL, *version = NULL, *sequence = NULL;
    TiredTransactionRecord record = {0};
    bool ok = false;
    uint64_t schema;
    if (!tired_json_parse(data, length, TIRED_TRANSACTION_RECORD_LIMIT, &document, error))
        goto done;
    if (!json_object_is_type(document, json_type_object) ||
        json_object_object_length(document) != 10 ||
        !json_object_object_get_ex(document, "schema_version", &version) ||
        !tired_json_u64(version, 1, 1, &schema, error) ||
        !json_object_object_get_ex(document, "sequence", &sequence) ||
        !tired_json_u64(sequence, 1, TIRED_TRANSACTION_SEQUENCE_LIMIT, &record.sequence, error) ||
        !fixed_field(document, "transaction_uuid", record.transaction_uuid, 36) ||
        !fixed_field(document, "service_uuid", record.service_uuid, 36) ||
        !fixed_field(document, "approved_sha256", record.approved_sha256, 64))
        goto schema_error;
    static const char *const scopes[] = {"system", "user"};
    int scope = choice(document, "scope", scopes, 2);
    int operation = choice(document, "operation", operations, TIRED_TRANSACTION_OPERATION_COUNT);
    int action = choice(document, "action", actions, TIRED_ACTION_COUNT);
    int state = choice(document, "state", states, TIRED_ACTION_STATE_COUNT);
    size_t name_length = 0;
    const char *name = string_field(document, "unit_name", &name_length);
    if (scope < 0 || operation < 0 || action < 0 || state < 0 || name == NULL)
        goto schema_error;
    record.user_scope = scope == 1;
    record.operation = (TiredTransactionOperation)operation;
    record.action = (TiredTransactionAction)action;
    record.state = (TiredTransactionActionState)state;
    if (!tired_text_set(&record.unit_name, name, name_length, TIRED_EXPLICIT_NAME_LIMIT + 8,
                        error) ||
        !validate(&record, error))
        goto done;
    tired_transaction_record_destroy(output);
    *output = record;
    record = (TiredTransactionRecord){0};
    tired_error_clear(error);
    ok = true;
    goto done;
schema_error:
    invalid(error);
done:
    json_object_put(document);
    tired_transaction_record_destroy(&record);
    return ok;
}
bool tired_transaction_record_encode(const TiredTransactionRecord *record, TiredText *output,
                                     TiredError *error)
{
    assert(record != NULL && output != NULL);
    if (!validate(record, error))
        return false;
    char buffer[1024];
    /* All strings have a validated JSON-safe ASCII grammar. Respect the owned
     * name length even for a caller-supplied nonterminated view. */
    int length = snprintf(
        buffer, sizeof(buffer),
        "{\"schema_version\":1,\"transaction_uuid\":\"%s\",\"service_uuid\":\"%s\","
        "\"unit_name\":\"%.*s\",\"scope\":\"%s\",\"operation\":\"%s\","
        "\"approved_sha256\":\"%s\",\"sequence\":%" PRIu64 ",\"action\":\"%s\",\"state\":\"%s\"}\n",
        record->transaction_uuid, record->service_uuid, (int)record->unit_name.length,
        record->unit_name.data, record->user_scope ? "user" : "system",
        operations[record->operation], record->approved_sha256, record->sequence,
        actions[record->action], states[record->state]);
    if (length < 0 || (size_t)length >= sizeof(buffer))
        return tired_error_set(error, TIRED_INTERNAL, "transaction-record-format",
                               "Cannot format transaction record.", 0);
    bool ok = tired_text_set(output, buffer, (size_t)length, TIRED_TRANSACTION_RECORD_LIMIT, error);
    if (ok)
        tired_error_clear(error);
    return ok;
}
