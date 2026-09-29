#include "tired/executable_evidence.h"
#include "tired/json.h"
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
int main(void)
{
    int result = 1;
    TiredInvocation invocation = {.executable = {.data = "/not-existing/link/../app", .length = 25},
                                  .resolved_target = {.data = "/not-existing/target", .length = 20},
                                  .device = (dev_t)-1,
                                  .inode = (ino_t)-1};
    invocation.executable.length = strlen(invocation.executable.data);
    invocation.resolved_target.length = strlen(invocation.resolved_target.data);
    TiredExecutableEvidence source = {0}, parsed = {0};
    TiredText encoded = {0}, again = {0};
    TiredError error = {0};
    struct json_object *object = NULL;
    CHECK(tired_executable_evidence_capture(&invocation, &source, &error));
    CHECK(source.lexical_path.data != invocation.executable.data);
    CHECK(strcmp(source.lexical_path.data, invocation.executable.data) == 0);
    CHECK(tired_executable_evidence_encode(&source, &encoded, &error));
    CHECK(tired_executable_evidence_parse(encoded.data, encoded.length, &parsed, &error));
    CHECK(parsed.device == (dev_t)-1 && parsed.inode == (ino_t)-1);
    CHECK(strcmp(parsed.resolved_path.data, "/not-existing/target") == 0);
    CHECK(tired_executable_evidence_encode(&parsed, &again, &error));
    CHECK(strcmp(encoded.data, again.data) == 0);
    invocation.executable = (TiredText){.data = "relative", .length = 8};
    CHECK(!tired_executable_evidence_capture(&invocation, &source, &error));
    CHECK(strcmp(source.lexical_path.data, "/not-existing/link/../app") == 0);
    CHECK(tired_json_parse(encoded.data, encoded.length, 65536, &object, &error));
    CHECK(json_object_object_add(object, "inode", json_object_new_double(1.0)) == 0);
    const char *bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_executable_evidence_parse(bad, strlen(bad), &parsed, &error));
    CHECK(parsed.inode == (ino_t)-1);
    CHECK(json_object_object_add(object, "inode", json_object_new_uint64(1)) == 0);
    CHECK(json_object_object_add(object, "lexical_path", json_object_new_string("/bad\npath")) ==
          0);
    bad = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    CHECK(!tired_executable_evidence_parse(bad, strlen(bad), &parsed, &error));
    CHECK(strcmp(parsed.lexical_path.data, source.lexical_path.data) == 0);
    result = 0;
cleanup:
    json_object_put(object);
    tired_executable_evidence_destroy(&source);
    tired_executable_evidence_destroy(&parsed);
    tired_text_destroy(&encoded);
    tired_text_destroy(&again);
    return result;
}
