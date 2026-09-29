#include "tired/journal_output.h"
#include "tired/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(void)
{
    int result = 1;
    unsigned char binary[] = {'a', 0, 27, '\n', 255, '\\'};
    TiredJournalRecord record = {
        .cursor = {.data = "s=fixture", .length = 9},
        .realtime_usec = UINT64_MAX,
        .monotonic_usec = 42,
        .boot_id = "0123456789abcdef0123456789abcdef",
        .message = {.data = binary, .length = sizeof(binary), .present = true},
        .pid = {.known = true, .value = 0},
        .priority = {.invalid = true}};
    TiredText output = {0};
    TiredError error = {0};
    struct json_object *object = NULL, *value = NULL, *field = NULL;
    unsigned char *large = NULL;
    CHECK(tired_journal_output(&record, "relay.service", true, 1000, true, &output, &error));
    CHECK(output.data[output.length - 1] == '\n' &&
          strchr(output.data, '\n') == output.data + output.length - 1);
    CHECK(tired_json_parse(output.data, output.length, 512U * 1024U, &object, &error));
    CHECK(json_object_object_get_ex(object, "event_type", &value));
    CHECK(strcmp(json_object_get_string(value), "journal_record") == 0);
    CHECK(json_object_object_get_ex(object, "selected_service", &value));
    CHECK(strcmp(json_object_get_string(value), "relay.service") == 0);
    CHECK(json_object_object_get_ex(object, "selected_uid", &value) &&
          json_object_get_uint64(value) == 1000);
    CHECK(json_object_object_get_ex(object, "realtime_usec", &value) &&
          json_object_get_uint64(value) == UINT64_MAX);
    CHECK(json_object_object_get_ex(object, "message", &field));
    CHECK(json_object_object_get_ex(field, "data", &value));
    CHECK(strcmp(json_object_get_string(value), "61001b0aff5c") == 0);
    CHECK(json_object_object_get_ex(object, "priority", &field));
    CHECK(json_object_object_get_ex(field, "invalid", &value) && json_object_get_boolean(value));
    CHECK(!json_object_object_get_ex(field, "value", &value));
    CHECK(tired_journal_output(&record, "relay.service", true, 1000, false, &output, &error));
    CHECK(strstr(output.data, "user relay.service uid=1000: a\\x00\\x1b\\x0a\\xff\\\\\n") != NULL);
    for (size_t i = 0; i + 1 < output.length; ++i)
        CHECK(output.data[i] >= 32 && output.data[i] <= 126);
    record.message.present = false;
    record.message.length = 0;
    record.message.data = NULL;
    CHECK(tired_journal_output(&record, "relay.service", false, 0, true, &output, &error));
    CHECK(tired_json_parse(output.data, output.length, 512U * 1024U, &object, &error));
    CHECK(!json_object_object_get_ex(object, "selected_uid", &value));
    CHECK(json_object_object_get_ex(object, "message", &field));
    CHECK(json_object_object_get_ex(field, "present", &value) && !json_object_get_boolean(value));
    CHECK(!json_object_object_get_ex(field, "data", &value));
    record.message.present = true;
    CHECK(tired_journal_output(&record, "relay.service", false, 0, true, &output, &error));
    CHECK(tired_json_parse(output.data, output.length, 512U * 1024U, &object, &error));
    CHECK(json_object_object_get_ex(object, "message", &field));
    CHECK(json_object_object_get_ex(field, "data", &value) &&
          json_object_get_string_len(value) == 0);
    large = malloc(TIRED_JOURNAL_MESSAGE_LIMIT);
    CHECK(large != NULL);
    memset(large, 255, TIRED_JOURNAL_MESSAGE_LIMIT);
    record.message = (TiredJournalBytes){
        .data = large, .length = TIRED_JOURNAL_MESSAGE_LIMIT, .present = true, .truncated = true};
    CHECK(tired_journal_output(&record, "relay.service", false, 0, false, &output, &error));
    CHECK(strstr(output.data, " [truncated]\n") != NULL);
    CHECK(output.length > 4U * TIRED_JOURNAL_MESSAGE_LIMIT && output.length < 512U * 1024U);
    CHECK(tired_journal_output(&record, "relay.service", false, 0, true, &output, &error));
    CHECK(tired_json_parse(output.data, output.length, 512U * 1024U, &object, &error));
    CHECK(json_object_object_get_ex(object, "message", &field));
    CHECK(json_object_object_get_ex(field, "truncated", &value) && json_object_get_boolean(value));
    char *saved = output.data;
    CHECK(!tired_journal_output(&record, "bad\n.service", false, 0, true, &output, &error));
    CHECK(output.data == saved);
    result = 0;
cleanup:
    free(large);
    json_object_put(object);
    tired_text_destroy(&output);
    return result;
}
