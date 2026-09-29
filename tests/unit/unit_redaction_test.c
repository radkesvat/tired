#include "tired/unit_redaction.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(void)
{
    int result = 1;
    TiredText output = {0};
    TiredTextList argv = {0};
    TiredEnvironment environment = {0};
    TiredRedaction redaction = {0};
    TiredError error = {0};
    bool classified[TIRED_ARGUMENT_LIMIT] = {0};
    classified[1] = true;
    const char *args[] = {"/app", "classified%private", "--token=old-private"};
    for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); ++i)
        CHECK(tired_text_list_append(&argv, args[i], strlen(args[i]), TIRED_ARGUMENT_LIMIT,
                                     TIRED_INPUT_LIMIT, &error));
    CHECK(tired_environment_set(&environment, "CUSTOM=environment-private", 26, TIRED_ENV_EXPLICIT,
                                true, &error));
    const char source[] =
        "# keep header\r\n[Unit]\nDescription=keep exact  spacing\n[Service]\n"
        "ExecStart = :/app --new public \"classified%%private\" --ToKeN=\"current\\sprivate\"\n"
        "ExecReload=/app old-private environment-private\n"
        "ExecStop=/app --password \"top\\\n# comment within continued word\nprivate\"\n"
        "Environment=\"PUBLIC=visible\" CUSTOM=changed-private COPY=environment-private "
        "AUTH=inline-private\n"
        "SetCredential=key:credential-private\nSetCredentialEncrypted=key:encrypted-private\n"
        "Environment=\n[Install]\nWantedBy=multi-user.target\n";
    CHECK(tired_unit_redact(source, strlen(source), &argv, classified, &environment, &output,
                            &redaction, &error));
    CHECK(redaction.sensitive && redaction.redacted);
    CHECK(strstr(output.data, "# Redacted, non-installable view;") == output.data);
    CHECK(strstr(output.data, "# keep header\r\n[Unit]\nDescription=keep exact  spacing\n") !=
          NULL);
    CHECK(strstr(output.data,
                 "ExecStart = :/app \"[redacted]\" public \"[redacted]\" \"[redacted]\"\n") !=
          NULL);
    CHECK(strstr(output.data, "ExecReload=/app \"[redacted]\" \"[redacted]\"\n") != NULL);
    CHECK(strstr(output.data, "ExecStop=/app --password \"[redacted]\"\n") != NULL);
    CHECK(strstr(output.data,
                 "Environment=\"PUBLIC=visible\" \"[redacted]\" \"[redacted]\" \"[redacted]\"\n") !=
          NULL);
    CHECK(strstr(output.data, "private") == NULL && strstr(output.data, "comment within") == NULL);
    CHECK(strstr(output.data, "SetCredential=\"[redacted]\"\n") != NULL &&
          strstr(output.data, "SetCredentialEncrypted=\"[redacted]\"\n") != NULL);
    CHECK(strstr(output.data, "Environment=\n[Install]\nWantedBy=multi-user.target\n") != NULL);
    char *previous = output.data;
    const char *bad[] = {"[Service]\nExecStart=/app --password \"unterminated\n",
                         "[Service]\nEnvironment=not-an-assignment\n",
                         "[Service]\nEnvironment=1INVALID=value\n",
                         "[Service]\nExecStart=/app \\x00\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        CHECK(!tired_unit_redact(bad[i], strlen(bad[i]), NULL, NULL, NULL, &output, &redaction,
                                 &error));
        CHECK(output.data == previous && redaction.redacted && redaction.sensitive);
    }
    const char plain[] = "# unchanged\r\n[Service]\nExecStart=:/app \"ordinary\" %%n "
                         "$HOME\nEnvironment=PUBLIC=visible\n";
    CHECK(tired_unit_redact(plain, strlen(plain), NULL, NULL, NULL, &output, &redaction, &error));
    CHECK(!redaction.redacted && !redaction.sensitive && output.length == strlen(plain) &&
          memcmp(output.data, plain, output.length) == 0);
    CHECK(tired_unit_redact(NULL, 0, NULL, NULL, NULL, &output, &redaction, &error));
    CHECK(output.length == 0 && !redaction.redacted);
    result = 0;
cleanup:
    tired_text_destroy(&output);
    tired_text_list_destroy(&argv);
    tired_environment_destroy(&environment);
    return result;
}
