#include "tired/json.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    struct json_object *value = NULL;
    TiredError error = {0};
    const char *valid[] = {"{}",
                           "[]",
                           "null",
                           "true",
                           "-9223372036854775808",
                           "18446744073709551615",
                           "{\"a\":1,\"b\":[\"é\",\"\\ud83d\\ude00\"]}",
                           "1.5",
                           " 0 \n"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        CHECK(tired_json_parse(valid[i], strlen(valid[i]), TIRED_INPUT_LIMIT, &value, &error));
    const char *invalid[] = {"",
                             "{\"x\":1,\"x\":2}",
                             "{\"x\":1,\"\\u0078\":2}",
                             "[NaN]",
                             "Infinity",
                             "1e99999",
                             "1 2",
                             "{}junk",
                             "{\"x\":1,}",
                             "[1,]",
                             "01",
                             "+1",
                             "1.",
                             ".5",
                             "1e",
                             "{x:1}",
                             "/*a*/{}",
                             "\"\\ud800\"",
                             "\"\\udc00\"",
                             "\"\\u0000\"",
                             "18446744073709551616",
                             "-9223372036854775809"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(!tired_json_parse(invalid[i], strlen(invalid[i]), TIRED_INPUT_LIMIT, &value, &error));
    CHECK(json_object_is_type(value, json_type_int) && json_object_get_int(value) == 0);
    CHECK(!tired_json_parse("{}\0{}", 5, TIRED_INPUT_LIMIT, &value, &error));
    CHECK(!tired_json_parse("{}", 2, 1, &value, &error));
    char deep[81];
    memset(deep, '[', 40);
    memset(deep + 40, ']', 40);
    deep[80] = '\0';
    CHECK(!tired_json_parse(deep, 80, TIRED_INPUT_LIMIT, &value, &error));
    CHECK(tired_json_parse("18446744073709551615", 20, TIRED_INPUT_LIMIT, &value, &error));
    uint64_t integer = 0;
    CHECK(tired_json_u64(value, 0, UINT64_MAX, &integer, &error));
    CHECK(integer == UINT64_MAX);
    CHECK(tired_json_parse("-1", 2, TIRED_INPUT_LIMIT, &value, &error));
    CHECK(!tired_json_u64(value, 0, UINT64_MAX, &integer, &error));
    CHECK(tired_json_parse("1.0", 3, TIRED_INPUT_LIMIT, &value, &error));
    CHECK(!tired_json_u64(value, 0, UINT64_MAX, &integer, &error));
    CHECK(integer == UINT64_MAX);
    json_object_put(value);
    return 0;
}
