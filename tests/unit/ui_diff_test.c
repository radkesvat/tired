#include "tired/ui_diff.h"
#include <stdio.h>
#include <string.h>
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            fprintf(stderr, "%d: %s [%s]\n", __LINE__, #value,                                     \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
static TiredText view(const char *text)
{
    return (TiredText){.data = (char *)text, .length = strlen(text)};
}
int main(void)
{
    int result = 1;
    TiredError error = {0};
    TiredText output = {0};
    TiredServiceRecord saved = {.environment_loaded = true},
                       proposed = {.environment_loaded = true};
    TiredText before = view("one\ntwo\nthree\nfour\n"),
              after = view("one\ninserted\ntwo\nthree\nfour\n");
    CHECK(tired_ui_unified_diff("saved", "proposed", &before, &after, &output, &error));
    CHECK(strcmp(output.data, "--- saved\n+++ proposed\n@@ -1,4 +1,5 @@\n one\n+inserted\n two\n"
                              " three\n four\n") == 0);
    CHECK(tired_ui_unified_diff("saved", "proposed", &after, &before, &output, &error));
    CHECK(strstr(output.data, "@@ -1,5 +1,4 @@") != NULL &&
          strstr(output.data, "-inserted\n two\n") != NULL && strstr(output.data, "-two") == NULL);
    before = view("a\nb\na\nc\n");
    after = view("a\na\nc\n");
    CHECK(tired_ui_unified_diff("saved", "proposed", &before, &after, &output, &error));
    CHECK(strstr(output.data, "-b\n a\n c\n") != NULL && strstr(output.data, "+a") == NULL);
    before = view("old\n1\n2\n3\n4\n5\n6\n7\n8\nlast\n");
    after = view("new\n1\n2\n3\n4\n5\n6\n7\n8\nend\n");
    CHECK(tired_ui_unified_diff("saved", "proposed", &before, &after, &output, &error));
    CHECK(strstr(output.data, "@@ -1,4 +1,4 @@") != NULL &&
          strstr(output.data, "@@ -7,4 +7,4 @@") != NULL && strstr(output.data, "-5\n") == NULL &&
          strstr(output.data, "+5\n") == NULL);
    before = view("same");
    after = view("same\n");
    CHECK(tired_ui_unified_diff("saved", "proposed", &before, &after, &output, &error));
    CHECK(strstr(output.data, "-same\n\\ No newline at end of file\n+same\n") != NULL);
    before = view("");
    CHECK(tired_ui_unified_diff("empty", "proposed", &before, &after, &output, &error));
    CHECK(strstr(output.data, "@@ -0,0 +1,1 @@\n+same\n") != NULL);
    CHECK(tired_ui_unified_diff("saved", "proposed", &after, &after, &output, &error));
    CHECK(strstr(output.data, "(No unit text changes.)") != NULL);
    char many_old[4096], many_new[4096];
    for (size_t i = 0; i < sizeof(many_old); i += 2)
    {
        many_old[i] = 'x';
        many_new[i] = 'y';
        many_old[i + 1] = many_new[i + 1] = '\n';
    }
    before = (TiredText){.data = many_old, .length = sizeof(many_old)};
    after = (TiredText){.data = many_new, .length = sizeof(many_new)};
    CHECK(tired_text_set(&output, "preserved", 9, 256, &error));
    CHECK(!tired_ui_unified_diff("saved", "proposed", &before, &after, &output, &error) &&
          strcmp(error.code, "diff-too-large") == 0 && strcmp(output.data, "preserved") == 0);
    CHECK(tired_spec_defaults(&saved.spec, &error));
    CHECK(tired_spec_append(&saved.spec, TIRED_FIELD_ARGV, "/bin/demo", 9, TIRED_ORIGIN_CAPTURE,
                            &error));
    CHECK(tired_spec_append(&saved.spec, TIRED_FIELD_ARGV, "SECRET-old", 10, TIRED_ORIGIN_CAPTURE,
                            &error));
    saved.review.sensitive_arguments[1] = true;
    for (unsigned i = 0; i < TIRED_FIELD_COUNT; ++i)
        CHECK(tired_spec_copy_field(&proposed.spec, &saved.spec, (TiredFieldId)i, &error));
    CHECK(tired_spec_clear_list(&proposed.spec, TIRED_FIELD_ARGV, TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&proposed.spec, TIRED_FIELD_ARGV, "/bin/demo", 9, TIRED_ORIGIN_CAPTURE,
                            &error));
    CHECK(tired_spec_append(&proposed.spec, TIRED_FIELD_ARGV, "SECRET-new", 10,
                            TIRED_ORIGIN_CAPTURE, &error));
    proposed.review.sensitive_arguments[1] = true;
    CHECK(tired_ui_field_diff(&saved, &proposed, &output, &error));
    CHECK(strstr(output.data, "(classified value or classification changed)") != NULL &&
          strstr(output.data, "SECRET-old") == NULL && strstr(output.data, "SECRET-new") == NULL);
    proposed.review.sensitive_arguments[1] = false;
    CHECK(tired_spec_clear_list(&proposed.spec, TIRED_FIELD_ARGV, TIRED_ORIGIN_USER, &error));
    CHECK(tired_spec_append(&proposed.spec, TIRED_FIELD_ARGV, "/bin/demo", 9, TIRED_ORIGIN_USER,
                            &error));
    CHECK(
        tired_spec_append(&proposed.spec, TIRED_FIELD_ARGV, "safe", 4, TIRED_ORIGIN_USER, &error));
    CHECK(tired_spec_append(&proposed.spec, TIRED_FIELD_ARGV, "SECRET-new", 10, TIRED_ORIGIN_USER,
                            &error));
    proposed.review.sensitive_arguments[2] = true;
    CHECK(tired_spec_set(&proposed.spec, TIRED_FIELD_START, "false", 5, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_spec_set(&proposed.spec, TIRED_FIELD_ENABLE, "false", 5, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(
        tired_spec_set(&proposed.spec, TIRED_FIELD_NICE, "-5", 2, TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_spec_inherit(&proposed.spec, TIRED_FIELD_MEMORY_MAX, &error));
    CHECK(tired_spec_set(&proposed.spec, TIRED_FIELD_DESCRIPTION, "Control \x1b[31m", 13,
                         TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_environment_set(&saved.environment, "CUSTOM=ENV-old", 14, TIRED_ENV_EXPLICIT, true,
                                &error));
    CHECK(tired_environment_set(&proposed.environment, "CUSTOM=ENV-new", 14, TIRED_ENV_EDITED,
                                false, &error));
    CHECK(tired_environment_set(&saved.environment, "ORIGIN=plain", 12, TIRED_ENV_EXPLICIT, false,
                                &error));
    CHECK(tired_environment_set(&proposed.environment, "ORIGIN=plain", 12, TIRED_ENV_EDITED, false,
                                &error));
    CHECK(tired_ui_field_diff(&saved, &proposed, &output, &error));
    CHECK(strstr(output.data, "start [boolean]: true (default) -> false (user)") != NULL &&
          strstr(output.data, "enable [boolean]: true (default) -> false (user)") != NULL &&
          strstr(output.data, "nice [integer]:") != NULL &&
          strstr(output.data, "memory_max [limit]: unset (inherited) -> inherit (user)") != NULL);
    CHECK(strstr(output.data, "argv [list]:") != NULL &&
          strstr(output.data,
                 "environment.CUSTOM [text]: [redacted] (explicit) -> [redacted] (edited)") !=
              NULL &&
          strstr(output.data,
                 "environment.ORIGIN [text]: \"plain\" (explicit) -> \"plain\" (edited)") != NULL &&
          strstr(output.data, "SECRET-old") == NULL && strstr(output.data, "SECRET-new") == NULL &&
          strstr(output.data, "ENV-old") == NULL && strstr(output.data, "ENV-new") == NULL);
    CHECK(strchr(output.data, '\x1b') == NULL && strstr(output.data, "\\u001b") != NULL);
    CHECK(tired_ui_field_diff(&saved, &saved, &output, &error));
    CHECK(strstr(output.data, "(No typed field changes.)") != NULL);
    result = 0;
cleanup:
    tired_service_record_destroy(&saved);
    tired_service_record_destroy(&proposed);
    tired_text_destroy(&output);
    return result;
}
