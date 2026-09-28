#include "tired/encode.h"
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
    TiredText output = {0};
    TiredError error = {0};
    CHECK(tired_encode_token("", 0, &output, &error));
    CHECK(strcmp(output.data, "\"\"") == 0);
    CHECK(tired_encode_token(";", 1, &output, &error));
    CHECK(strcmp(output.data, "\";\"") == 0);
    const char *input = "$HOME ${TOKEN} %n \\\"\n\t";
    CHECK(tired_encode_token(input, strlen(input), &output, &error));
    CHECK(strcmp(output.data, "\"$HOME ${TOKEN} %%n \\\\\\\"\\x0a\\x09\"") == 0);
    const char *prefixes[] = {"-", "+", "!", ":", "@", "|", ">"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
    {
        CHECK(tired_encode_token(prefixes[i], 1, &output, &error));
        CHECK(output.length == 3 && output.data[0] == '"' && output.data[1] == prefixes[i][0]);
    }
    CHECK(tired_encode_directive("a%nb", 4, &output, &error));
    CHECK(strcmp(output.data, "a%%nb") == 0);
    CHECK(!tired_encode_directive("a\nb", 3, &output, &error));
    CHECK(!tired_encode_directive(" x", 2, &output, &error));
    CHECK(!tired_encode_directive("x ", 2, &output, &error));
    CHECK(!tired_encode_directive("x\\", 2, &output, &error));
    CHECK(strcmp(output.data, "a%%nb") == 0);
    CHECK(!tired_encode_token("a\0b", 3, &output, &error));
    CHECK(tired_encode_display("\033[31m\n\\é\xc2\x9b", strlen("\033[31m\n\\é\xc2\x9b"), &output,
                               &error));
    CHECK(strcmp(output.data, "\\x1b[31m\\x0a\\\\é\\u009b") == 0);
    TiredBuffer buffer;
    tired_buffer_init(&buffer, 3);
    CHECK(tired_buffer_append(&buffer, "abc", 3, &error));
    CHECK(!tired_buffer_append(&buffer, "d", 1, &error));
    CHECK(buffer.length == 3 && strcmp(buffer.data, "abc") == 0);
    CHECK(tired_buffer_take(&buffer, &output, &error));
    CHECK(strcmp(output.data, "abc") == 0 && buffer.data == NULL);
    tired_buffer_destroy(&buffer);
    tired_buffer_init(&buffer, 0);
    CHECK(tired_buffer_take(&buffer, &output, &error));
    CHECK(output.data != NULL && output.length == 0);
    tired_buffer_destroy(&buffer);
    tired_text_destroy(&output);
    return 0;
}
