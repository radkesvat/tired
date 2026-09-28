#include "tired/value.h"

#include <stdio.h>
#include <string.h>

/* Checks remain active in optimized NDEBUG builds. */
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
    TiredError error = {0};
    uint64_t unsigned_value = 42;
    CHECK(tired_parse_u64("18446744073709551615", 20, 0, UINT64_MAX, &unsigned_value, &error));
    CHECK(unsigned_value == UINT64_MAX);
    CHECK(!tired_parse_u64("18446744073709551616", 20, 0, UINT64_MAX, &unsigned_value, &error));
    CHECK(unsigned_value == UINT64_MAX && error.status == TIRED_INVALID);
    const char *bad[] = {"", "-1", "+1", " 1", "1 ", "1.0", "1e2", "0x10", "12x"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!tired_parse_u64(bad[i], strlen(bad[i]), 0, UINT64_MAX, &unsigned_value, &error));
    CHECK(!tired_parse_u64("1\0002", 3, 0, UINT64_MAX, &unsigned_value, &error));
    CHECK(tired_parse_u64("0", 1, 0, 0, &unsigned_value, &error));
    CHECK(!tired_parse_u64("1", 1, 0, 0, &unsigned_value, &error));
    CHECK(!tired_parse_u64("0", 1, 1, 2, &unsigned_value, &error));
    int64_t signed_value = 42;
    CHECK(tired_parse_i64("-9223372036854775808", 20, INT64_MIN, INT64_MAX, &signed_value, &error));
    CHECK(signed_value == INT64_MIN);
    CHECK(
        !tired_parse_i64("-9223372036854775809", 20, INT64_MIN, INT64_MAX, &signed_value, &error));
    CHECK(signed_value == INT64_MIN);
    CHECK(tired_parse_i64("9223372036854775807", 19, INT64_MIN, INT64_MAX, &signed_value, &error));
    CHECK(signed_value == INT64_MAX);
    CHECK(!tired_parse_i64("9223372036854775808", 19, INT64_MIN, INT64_MAX, &signed_value, &error));
    CHECK(!tired_parse_i64("-", 1, INT64_MIN, INT64_MAX, &signed_value, &error));
    CHECK(!tired_parse_i64("-1", 2, 0, 19, &signed_value, &error));
    CHECK(!tired_parse_i64("20", 2, -20, 19, &signed_value, &error));

    TiredText text = {0};
    CHECK(tired_text_set(&text, "a\nb", 3, 3, &error));
    CHECK(text.length == 3 && memcmp(text.data, "a\nb", 4) == 0);
    CHECK(!tired_text_set(&text, "1234", 4, 3, &error));
    CHECK(text.length == 3 && strcmp(text.data, "a\nb") == 0);
    CHECK(!tired_text_set(&text, "a\0b", 3, 3, &error));
    CHECK(!tired_text_set(&text, "", SIZE_MAX, SIZE_MAX, &error));
    CHECK(tired_text_set(&text, text.data + 1, 2, 3, &error));
    CHECK(strcmp(text.data, "\nb") == 0);
    CHECK(tired_text_set(&text, NULL, 0, 0, &error));
    CHECK(text.data != NULL && text.length == 0 && text.data[0] == '\0');
    tired_text_destroy(&text);
    tired_text_destroy(&text);
    CHECK(text.data == NULL && text.length == 0);

    TiredTextList list = {0};
    CHECK(tired_text_list_append(&list, "", 0, 3, 4, &error));
    CHECK(tired_text_list_append(&list, "ab", 2, 3, 4, &error));
    CHECK(list.count == 2 && list.bytes == 4);
    CHECK(!tired_text_list_append(&list, "", 0, 3, 4, &error));
    CHECK(list.count == 2 && strcmp(list.items[1].data, "ab") == 0);
    CHECK(!tired_text_list_append(&list, "", 0, 2, 100, &error));
    CHECK(tired_text_list_append(&list, list.items[1].data, 2, 3, 7, &error));
    CHECK(list.count == 3 && list.bytes == 7 && strcmp(list.items[2].data, "ab") == 0);
    tired_text_list_destroy(&list);
    tired_text_list_destroy(&list);
    CHECK(list.items == NULL && list.count == 0 && list.bytes == 0);
    CHECK(error.status == TIRED_OK);
    return 0;
}
