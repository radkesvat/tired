#include "tired/redaction.h"
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
    TiredServiceSpec source = {0}, display = {0};
    TiredError error = {0};
    TiredRedaction redaction = {0};
    bool classified[TIRED_ARGUMENT_LIMIT] = {0};
    CHECK(tired_spec_display(&source, NULL, false, &display, &redaction, &error));
    CHECK(display.fields[TIRED_FIELD_ARGV].origin == TIRED_ORIGIN_UNSET && !redaction.redacted &&
          !redaction.sensitive);
    CHECK(tired_spec_inherit(&source, TIRED_FIELD_RESTART, &error));
    const char *args[] = {"/private-token-program",
                          "--PaSsWoRd",
                          "secret-value",
                          "--token=attached",
                          "ordinary",
                          "",
                          "--foo",
                          "visible",
                          "--SECRET"};
    for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); ++i)
        CHECK(tired_spec_append(&source, TIRED_FIELD_ARGV, args[i], strlen(args[i]),
                                TIRED_ORIGIN_USER, &error));
    classified[0] = true; /* argv[0] remains the executable even for a bad caller mask. */
    classified[4] = classified[5] = true;
    CHECK(tired_spec_display(&source, classified, false, &display, &redaction, &error));
    CHECK(redaction.redacted && redaction.sensitive &&
          display.fields[TIRED_FIELD_ARGV].origin == TIRED_ORIGIN_USER &&
          display.fields[TIRED_FIELD_RESTART].inherit);
    const TiredTextList *list = &display.fields[TIRED_FIELD_ARGV].value.list;
    CHECK(list->count == 9 && strcmp(list->items[0].data, args[0]) == 0);
    CHECK(strcmp(list->items[2].data, "[redacted]") == 0 &&
          strcmp(list->items[3].data, "[redacted]") == 0 &&
          strcmp(list->items[4].data, "[redacted]") == 0 &&
          strcmp(list->items[5].data, "[redacted]") == 0 &&
          strcmp(list->items[7].data, "visible") == 0);
    CHECK(strcmp(source.fields[TIRED_FIELD_ARGV].value.list.items[2].data, "secret-value") == 0);
    CHECK(tired_spec_display(&source, classified, true, &display, &redaction, &error));
    CHECK(!redaction.redacted && redaction.sensitive &&
          strcmp(display.fields[TIRED_FIELD_ARGV].value.list.items[2].data, "secret-value") == 0);
    CHECK(display.fields[TIRED_FIELD_ARGV].value.list.items[2].data !=
          source.fields[TIRED_FIELD_ARGV].value.list.items[2].data);
    CHECK(tired_spec_clear_list(&source, TIRED_FIELD_ARGV, TIRED_ORIGIN_PROFILE, &error));
    char long_flag[220];
    memset(long_flag, 'x', sizeof(long_flag));
    long_flag[0] = '-';
    memcpy(long_flag + 180, "API-KEY=hidden", 15);
    long_flag[195] = '\0';
    CHECK(tired_spec_append(&source, TIRED_FIELD_ARGV, "/app", 4, TIRED_ORIGIN_PROFILE, &error));
    CHECK(tired_spec_append(&source, TIRED_FIELD_ARGV, long_flag, strlen(long_flag),
                            TIRED_ORIGIN_PROFILE, &error));
    CHECK(tired_spec_display(&source, NULL, false, &source, &redaction, &error));
    CHECK(redaction.redacted && source.fields[TIRED_FIELD_ARGV].origin == TIRED_ORIGIN_PROFILE &&
          strcmp(source.fields[TIRED_FIELD_ARGV].value.list.items[1].data, "[redacted]") == 0);
    CHECK(tired_spec_clear_list(&source, TIRED_FIELD_ARGV, TIRED_ORIGIN_USER, &error));
    CHECK(tired_spec_display(&source, NULL, false, &display, &redaction, &error));
    CHECK(!redaction.redacted && !redaction.sensitive &&
          display.fields[TIRED_FIELD_ARGV].origin == TIRED_ORIGIN_USER &&
          display.fields[TIRED_FIELD_ARGV].value.list.count == 0);
    result = 0;
cleanup:
    tired_spec_destroy(&source);
    tired_spec_destroy(&display);
    return result;
}
