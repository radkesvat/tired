#include "tired/encode.h"
#include "tired/unit_document.h"
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
    TiredUnitDocument document = {0};
    TiredUnitWords words = {0};
    TiredError error = {0};
    char *many = NULL;
    const char source[] =
        "# header\r\n[Unit]\n Description = test # literal\r"
        "[Service]\nExecStart = /app \"top\\\n # ignored\\\n; ignored too\nsecret\"\n"
        "Environment=\nEnvironment = A=B\n[Install]\nWantedBy=multi-user.target";
    CHECK(tired_unit_document_parse(source, strlen(source), &document, &error));
    CHECK(document.count == 5);
    CHECK(strcmp(document.assignments[0].section.data, "Unit") == 0 &&
          strcmp(document.assignments[0].key.data, "Description") == 0 &&
          strcmp(document.assignments[0].value.data, "test # literal") == 0);
    const TiredUnitAssignment *assignment = &document.assignments[1];
    CHECK(strcmp(assignment->section.data, "Service") == 0 &&
          strcmp(assignment->key.data, "ExecStart") == 0 &&
          strcmp(assignment->value.data, "/app \"top secret\"") == 0);
    for (size_t i = 0; i < document.count; ++i)
    {
        const TiredUnitAssignment *entry = &document.assignments[i];
        for (size_t j = 0; j < entry->value.length; ++j)
        {
            CHECK(entry->value_offsets[j] < strlen(source));
            CHECK(entry->value.data[j] == source[entry->value_offsets[j]] ||
                  (entry->value.data[j] == ' ' && source[entry->value_offsets[j]] == '\\'));
            if (j != 0)
                CHECK(entry->value_offsets[j - 1] < entry->value_offsets[j]);
        }
    }
    CHECK(tired_unit_words_parse(assignment->value.data, assignment->value.length, &words, &error));
    CHECK(words.count == 2 && strcmp(words.words[1].value.data, "top secret") == 0);
    size_t start = assignment->value_offsets[words.words[1].start];
    size_t end = assignment->value_offsets[words.words[1].end - 1] + 1;
    const char span[] = "\"top\\\n # ignored\\\n; ignored too\nsecret\"";
    CHECK(end - start == strlen(span) && memcmp(source + start, span, strlen(span)) == 0);
    CHECK(document.assignments[2].value.length == 0 &&
          document.assignments[2].value_offsets == NULL);
    CHECK(strcmp(document.assignments[3].key.data, "Environment") == 0);
    TiredUnitAssignment *saved = document.assignments;
    const char *bad[] = {"Key=value",       "[Service",           "[]",
                         "[Service]\n=bad", "[Service]\ninvalid", "[Service]\nKey=\xff"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(!tired_unit_document_parse(bad[i], strlen(bad[i]), &document, &error));
        CHECK(document.assignments == saved && document.count == 5);
    }
    CHECK(!tired_unit_document_parse("[Service]\nK=a\0b", 15, &document, &error));
    const char ending[] = "\xef\xbb\xbf[Service]\r\nKey=one\\\\\nOther=two\\\n# ignored at EOF";
    CHECK(tired_unit_document_parse(ending, strlen(ending), &document, &error));
    CHECK(document.count == 2 && strcmp(document.assignments[0].value.data, "one\\\\") == 0 &&
          strcmp(document.assignments[1].value.data, "two") == 0);
    const char blank[] = "[Service]\nKey=one\\\n\nOther=two\n";
    CHECK(tired_unit_document_parse(blank, strlen(blank), &document, &error));
    CHECK(document.count == 2 && strcmp(document.assignments[0].value.data, "one") == 0);
    many = malloc(TIRED_INPUT_LIMIT + 20);
    CHECK(many != NULL);
    memcpy(many, "[Service]\n", 10);
    for (size_t i = 0; i < 4097; ++i)
        memcpy(many + 10 + i * 4, "K=v\n", 4);
    CHECK(tired_unit_document_parse(many, 10 + 4096 * 4, &document, &error));
    CHECK(document.count == 4096);
    CHECK(!tired_unit_document_parse(many, 10 + 4097 * 4, &document, &error));
    CHECK(document.count == 4096);
    memcpy(many + 10, "K=", 2);
    memset(many + 12, 'v', TIRED_INPUT_LIMIT);
    CHECK(!tired_unit_document_parse(many, 12 + TIRED_INPUT_LIMIT, &document, &error));
    CHECK(tired_unit_document_parse(NULL, 0, &document, &error));
    CHECK(document.count == 0);
    result = 0;
cleanup:
    free(many);
    tired_unit_words_destroy(&words);
    tired_unit_document_destroy(&document);
    return result;
}
