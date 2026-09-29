#include "tired/json.h"
#include "tired/model_format.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            fprintf(stderr, "%s: %s\n", error.code == NULL ? "none" : error.code,                  \
                    error.message == NULL ? "" : error.message);                                   \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(void)
{
    int result = 1;
    TiredServiceSpec source = {0}, parsed = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *document = NULL, *fields = NULL, *entry = NULL;
    CHECK(tired_spec_encode(&source, &encoded, &error));
    CHECK(tired_spec_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.fields[TIRED_FIELD_SCOPE].origin == TIRED_ORIGIN_UNSET);
    CHECK(tired_spec_defaults(&source, &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_NAME, "relay.service.service", 21, TIRED_ORIGIN_USER,
                         true, &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_NICE, "-5", 2, TIRED_ORIGIN_PROFILE, true, &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_RESTART_SEC, "1.234567s", 9,
                         TIRED_ORIGIN_CONFIG_ADMIN, true, &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_CPU_QUOTA, "123.45%", 7, TIRED_ORIGIN_CONFIG_USER,
                         true, &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_UMASK, "0027", 4, TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_TIMEOUT_START, "infinity", 8, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_TIMEOUT_STOP, "42us", 4, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_set(&source, TIRED_FIELD_MEMORY_MAX, "18446744073709551614", 20,
                         TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_spec_inherit(&source, TIRED_FIELD_PRIVATE_TMP, &error));
    CHECK(tired_spec_clear_list(&source, TIRED_FIELD_AMBIENT_CAPABILITIES, TIRED_ORIGIN_USER,
                                &error));
    CHECK(tired_spec_append(&source, TIRED_FIELD_ARGV, "/bin/example", 12, TIRED_ORIGIN_CAPTURE,
                            &error));
    CHECK(tired_spec_append(&source, TIRED_FIELD_ARGV, "", 0, TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&source, TIRED_FIELD_ARGV, "a b\nsecret", 10, TIRED_ORIGIN_CAPTURE,
                            &error));
    CHECK(tired_spec_encode(&source, &encoded, &error));
    CHECK(tired_spec_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(strcmp(parsed.fields[TIRED_FIELD_NAME].value.text.data, "relay.service") == 0);
    CHECK(parsed.fields[TIRED_FIELD_NAME].origin == TIRED_ORIGIN_USER);
    CHECK(parsed.fields[TIRED_FIELD_NICE].value.integer == -5 &&
          parsed.fields[TIRED_FIELD_NICE].origin == TIRED_ORIGIN_PROFILE);
    CHECK(parsed.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 1234567);
    CHECK(parsed.fields[TIRED_FIELD_CPU_QUOTA].value.quota == 12345);
    CHECK(parsed.fields[TIRED_FIELD_UMASK].value.mode == 0027);
    CHECK(parsed.fields[TIRED_FIELD_TIMEOUT_START].value.timeout.infinity);
    CHECK(parsed.fields[TIRED_FIELD_TIMEOUT_STOP].value.timeout.value == 42);
    CHECK(parsed.fields[TIRED_FIELD_MEMORY_MAX].value.limit.value == UINT64_MAX - 1 &&
          !parsed.fields[TIRED_FIELD_MEMORY_MAX].value.limit.infinity);
    CHECK(parsed.fields[TIRED_FIELD_PRIVATE_TMP].inherit &&
          parsed.fields[TIRED_FIELD_PRIVATE_TMP].origin == TIRED_ORIGIN_USER);
    CHECK(tired_field_has_value(&parsed.fields[TIRED_FIELD_AMBIENT_CAPABILITIES]) &&
          parsed.fields[TIRED_FIELD_AMBIENT_CAPABILITIES].value.list.count == 0);
    CHECK(parsed.fields[TIRED_FIELD_ARGV].value.list.count == 3 &&
          parsed.fields[TIRED_FIELD_ARGV].value.list.items[1].length == 0);
    CHECK(tired_spec_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    CHECK(tired_json_parse(encoded.data, encoded.length, TIRED_INPUT_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "fields", &fields));
    CHECK(json_object_object_get_ex(fields, "nice", &entry));
    CHECK(json_object_object_add(entry, "value", json_object_new_int(-5)) == 0);
    const char *bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_spec_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.fields[TIRED_FIELD_NICE].value.integer == -5);
    CHECK(json_object_object_add(entry, "value", json_object_new_string("-5")) == 0);
    CHECK(json_object_object_add(entry, "origin", json_object_new_string("unset")) == 0);
    bad = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_spec_parse(bad, strlen(bad), &parsed, &error));
    result = 0;
cleanup:
    json_object_put(document);
    tired_spec_destroy(&source);
    tired_spec_destroy(&parsed);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
