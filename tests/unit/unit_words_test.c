#include "tired/encode.h"
#include "tired/unit_words.h"
#include <stdio.h>
#include <stdlib.h>
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
    TiredUnitWords words = {0};
    TiredText encoded = {0};
    TiredError error = {0};
    char *many = NULL;
    const char source[] =
        " \t:/app  \"\" 'two words' --token=\"a\\sb\" \\\"quote\\\" %%n $HOME ; #text";
    CHECK(tired_unit_words_parse(source, strlen(source), &words, &error));
    CHECK(words.count == 9 && words.words[0].start == 2 && words.words[0].end == 7);
    const char *expected[] = {":/app", "",      "two words", "--token=a b", "\"quote\"",
                              "%%n",   "$HOME", ";",         "#text"};
    for (size_t i = 0; i < words.count; ++i)
    {
        CHECK(strcmp(words.words[i].value.data, expected[i]) == 0);
        CHECK(words.words[i].start < words.words[i].end && words.words[i].end <= strlen(source));
        if (i != 0)
            CHECK(words.words[i - 1].end < words.words[i].start);
    }
    CHECK(memcmp(source + words.words[3].start, "--token=\"a\\sb\"",
                 words.words[3].end - words.words[3].start) == 0);
    const char escapes[] =
        "'\\a\\b\\f\\n\\r\\t\\v\\\\\\\"\\\'\\s' \\x41\\101 \\u00e9\\U0001f642 \\xff";
    CHECK(tired_unit_words_parse(escapes, strlen(escapes), &words, &error));
    CHECK(words.count == 4 && strcmp(words.words[0].value.data, "\a\b\f\n\r\t\v\\\"' ") == 0);
    CHECK(strcmp(words.words[1].value.data, "AA") == 0);
    CHECK(strcmp(words.words[2].value.data, "\xc3\xa9\xf0\x9f\x99\x82") == 0);
    CHECK(words.words[3].value.length == 1 && (unsigned char)words.words[3].value.data[0] == 255);
    const char *bad[] = {"'open",   "\"open",  "trailing\\",  "\\q",    "\\x0",
                         "\\xgg",   "\\000",   "\\400",       "\\x00",  "\\u0000",
                         "\\ud800", "\\uffff", "\\U00110000", "\\u123", "\\  "};
    TiredUnitWord *saved = words.words;
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(!tired_unit_words_parse(bad[i], strlen(bad[i]), &words, &error));
        CHECK(words.words == saved && words.count == 4);
    }
    CHECK(!tired_unit_words_parse("a\0b", 3, &words, &error));
    CHECK(words.words == saved);
    const char literal[] = "a b\t\n\"'\\\xc3\xa9";
    CHECK(tired_encode_token(literal, strlen(literal), &encoded, &error));
    CHECK(tired_unit_words_parse(encoded.data, encoded.length, &words, &error));
    CHECK(words.count == 1 && strcmp(words.words[0].value.data, literal) == 0 &&
          words.words[0].start == 0 && words.words[0].end == encoded.length);
    many = malloc(TIRED_INPUT_LIMIT + 1);
    CHECK(many != NULL);
    for (size_t i = 0; i < 4097; ++i)
    {
        many[i * 2] = 'a';
        many[i * 2 + 1] = ' ';
    }
    CHECK(tired_unit_words_parse(many, 8192, &words, &error));
    CHECK(words.count == 4096);
    CHECK(!tired_unit_words_parse(many, 8194, &words, &error));
    CHECK(words.count == 4096);
    memset(many, 'a', TIRED_INPUT_LIMIT);
    CHECK(!tired_unit_words_parse(many, TIRED_INPUT_LIMIT, &words, &error));
    CHECK(tired_unit_words_parse(many, TIRED_INPUT_LIMIT - 1, &words, &error));
    CHECK(words.count == 1 && words.words[0].value.length == TIRED_INPUT_LIMIT - 1);
    CHECK(tired_unit_words_parse(NULL, 0, &words, &error));
    CHECK(words.count == 0);
    result = 0;
cleanup:
    free(many);
    tired_text_destroy(&encoded);
    tired_unit_words_destroy(&words);
    return result;
}
