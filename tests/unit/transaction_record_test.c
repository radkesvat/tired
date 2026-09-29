#include "tired/io.h"
#include "tired/json.h"
#include "tired/transaction_record.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    int result = 1;
    TiredTransactionRecord record = {
        .transaction_uuid = "01234567-89ab-4cde-8fab-0123456789ab",
        .service_uuid = "fedcba98-7654-4321-abcd-fedcba987654",
        .approved_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .unit_name = {.data = "relay.service", .length = 13},
        .operation = TIRED_TRANSACTION_CREATE,
        .action = TIRED_ACTION_START,
        .state = TIRED_ACTION_UNCERTAIN,
        .sequence = 7};
    TiredTransactionRecord parsed = {0};
    TiredText encoded = {0}, schema = {0};
    TiredError error = {0};
    struct json_object *document = NULL;
    CHECK(argc == 2);
    CHECK(tired_read_file(argv[1], TIRED_INPUT_LIMIT, &schema, &error));
    CHECK(tired_json_parse(schema.data, schema.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_is_type(document, json_type_object));
    json_object_put(document);
    document = NULL;
    CHECK(tired_transaction_record_encode(&record, &encoded, &error));
    CHECK(tired_transaction_record_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.sequence == 7 && parsed.state == TIRED_ACTION_UNCERTAIN);
    CHECK(strcmp(parsed.unit_name.data, "relay.service") == 0);
    CHECK(strcmp(parsed.approved_sha256, record.approved_sha256) == 0);
    for (unsigned i = 0; i < TIRED_ACTION_COUNT; ++i)
    {
        record.action = (TiredTransactionAction)i;
        record.operation = (TiredTransactionOperation)(i % TIRED_TRANSACTION_OPERATION_COUNT);
        record.state = (TiredTransactionActionState)(i % TIRED_ACTION_STATE_COUNT);
        record.user_scope = (i % 2) != 0;
        CHECK(tired_transaction_record_encode(&record, &encoded, &error));
        CHECK(tired_transaction_record_parse(encoded.data, encoded.length, &parsed, &error));
        CHECK(parsed.action == record.action && parsed.operation == record.operation &&
              parsed.state == record.state && parsed.user_scope == record.user_scope);
    }
    const char *keys[] = {"schema_version", "sequence",        "sequence",  "action",
                          "state",          "scope",           "operation", "transaction_uuid",
                          "service_uuid",   "approved_sha256", "unit_name", "extra"};
    const char *values[] = {"2",
                            "0",
                            "4097",
                            "\"shell\"",
                            "\"success\"",
                            "\"global\"",
                            "\"install\"",
                            "\"01234567-89AB-4cde-8fab-0123456789ab\"",
                            "\"fedcba98-7654-1321-abcd-fedcba987654\"",
                            "\"not-a-digest\"",
                            "\"../escape.service\"",
                            "true"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
    {
        CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_TRANSACTION_RECORD_LIMIT,
                               &document, &error));
        struct json_object *value = json_tokener_parse(values[i]);
        CHECK(value != NULL && json_object_object_add(document, keys[i], value) == 0);
        const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
        CHECK(!tired_transaction_record_parse(bad, strlen(bad), &parsed, &error));
        CHECK(parsed.sequence == 7 && strcmp(parsed.unit_name.data, "relay.service") == 0);
        json_object_put(document);
        document = NULL;
    }
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_TRANSACTION_RECORD_LIMIT, &document,
                           &error));
    json_object_object_del(document, "approved_sha256");
    const char *missing = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_transaction_record_parse(missing, strlen(missing), &parsed, &error));
    CHECK(!tired_transaction_record_parse("{\"sequence\":1,\"sequence\":2}", 27, &parsed, &error));
    record.sequence = 0;
    CHECK(!tired_transaction_record_encode(&record, &encoded, &error));
    CHECK(strstr(encoded.data, "\"sequence\":7") != NULL);
    record.sequence = 1;
    record.action = (TiredTransactionAction)-1;
    CHECK(!tired_transaction_record_encode(&record, &encoded, &error));
    record.action = TIRED_ACTION_PREPARE;
    memset(record.transaction_uuid, 'a', sizeof(record.transaction_uuid));
    CHECK(!tired_transaction_record_encode(&record, &encoded, &error));
    result = 0;
cleanup:
    json_object_put(document);
    tired_transaction_record_destroy(&parsed);
    tired_text_destroy(&encoded);
    tired_text_destroy(&schema);
    return result;
}
