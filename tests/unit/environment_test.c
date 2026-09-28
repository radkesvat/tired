#include "tired/environment.h"
#include <stdio.h>
#include <stdlib.h>
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
    TiredEnvironment env = {0};
    TiredCredentials credentials = {0};
    TiredError error = {0};
    const char *inputs[] = {"X=default", "X=profile",  "X=config", "X=imported",
                            "X=passed",  "X=explicit", "X=edited"};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
    {
        CHECK(tired_environment_set(&env, inputs[i], strlen(inputs[i]), (TiredEnvironmentOrigin)i,
                                    false, &error));
        const TiredEnvironmentEntry *entry = tired_environment_find(&env, "X", 1);
        CHECK(entry != NULL && strcmp(entry->value.data, strchr(inputs[i], '=') + 1) == 0);
        CHECK(entry->origin == (TiredEnvironmentOrigin)i);
    }
    CHECK(tired_environment_set(&env, "X=lower", 7, TIRED_ENV_IMPORTED, false, &error));
    CHECK(strcmp(tired_environment_find(&env, "X", 1)->value.data, "edited") == 0);
    CHECK(tired_environment_set(&env, "X=", 2, TIRED_ENV_EDITED, false, &error));
    CHECK(env.count == 1 && env.bytes == 3);
    CHECK(tired_environment_set(&env, "API_TOKEN=secret", 16, TIRED_ENV_EXPLICIT, false, &error));
    const TiredEnvironmentEntry *secret = tired_environment_find(&env, "API_TOKEN", 9);
    CHECK(secret != NULL && secret->sensitive &&
          strcmp(tired_environment_display(secret), "[redacted]") == 0);
    CHECK(tired_environment_set(&env, "PLAIN=first", 11, TIRED_ENV_EXPLICIT, true, &error));
    CHECK(tired_environment_set(&env, "PLAIN=second", 12, TIRED_ENV_EDITED, false, &error));
    CHECK(tired_environment_find(&env, "PLAIN", 5)->sensitive);
    CHECK(tired_environment_set(&env, "LITERAL=$HOME;%n\\text\n",
                                strlen("LITERAL=$HOME;%n\\text\n"), TIRED_ENV_EXPLICIT, false,
                                &error));
    CHECK(strcmp(tired_environment_find(&env, "LITERAL", 7)->value.data, "$HOME;%n\\text\n") == 0);
    size_t before = env.count;
    const char *bad[] = {"", "NO_EQUALS", "=value", "1KEY=x", "A-B=x", "export A=x", "A B=x"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!tired_environment_set(&env, bad[i], strlen(bad[i]), TIRED_ENV_EXPLICIT, false,
                                     &error));
    CHECK(!tired_environment_set(&env, "A=x\0y", 5, TIRED_ENV_EXPLICIT, false, &error));
    CHECK(env.count == before);
    CHECK(setenv("TIRED_UNIT_SELECTED_VALUE", "kept", 1) == 0);
    CHECK(setenv("TIRED_UNIT_UNSELECTED_VALUE", "ignored", 1) == 0);
    CHECK(tired_environment_pass(&env, "TIRED_UNIT_SELECTED_VALUE",
                                 sizeof("TIRED_UNIT_SELECTED_VALUE") - 1, &error));
    CHECK(tired_environment_find(&env, "TIRED_UNIT_UNSELECTED_VALUE",
                                 sizeof("TIRED_UNIT_UNSELECTED_VALUE") - 1) == NULL);
    CHECK(strcmp(tired_environment_find(&env, "TIRED_UNIT_SELECTED_VALUE",
                                        sizeof("TIRED_UNIT_SELECTED_VALUE") - 1)
                     ->value.data,
                 "kept") == 0);
    CHECK(unsetenv("TIRED_UNIT_SELECTED_VALUE") == 0);
    CHECK(!tired_environment_pass(&env, "TIRED_UNIT_SELECTED_VALUE",
                                  sizeof("TIRED_UNIT_SELECTED_VALUE") - 1, &error));
    CHECK(unsetenv("TIRED_UNIT_UNSELECTED_VALUE") == 0);
    CHECK(tired_credentials_add(&credentials, "token=/nonexistent/private file",
                                strlen("token=/nonexistent/private file"), &error));
    CHECK(credentials.count == 1);
    const char *file = "# comment\n; comment\n A = unquoted words  \nB=' literal $HOME\nline '\n"
                       "C=\"quote\\\" backslash\\\\ dollar\\$ tick\\` unknown\\q\"\n"
                       "D=con\\\ntinued\nE=trailing\\ \nEMPTY=\nX=imported\n";
    CHECK(tired_environment_import(&env, file, strlen(file), &error));
    CHECK(strcmp(tired_environment_find(&env, "A", 1)->value.data, "unquoted words") == 0);
    CHECK(strcmp(tired_environment_find(&env, "B", 1)->value.data, " literal $HOME\nline ") == 0);
    CHECK(strcmp(tired_environment_find(&env, "C", 1)->value.data,
                 "quote\" backslash\\ dollar$ tick` unknown\\q") == 0);
    CHECK(strcmp(tired_environment_find(&env, "D", 1)->value.data, "continued") == 0);
    CHECK(strcmp(tired_environment_find(&env, "E", 1)->value.data, "trailing ") == 0);
    CHECK(strcmp(tired_environment_find(&env, "X", 1)->value.data, "") == 0);
    CHECK(!tired_environment_import(&env, "A=changed\nBROKEN='", strlen("A=changed\nBROKEN='"),
                                    &error));
    CHECK(strcmp(tired_environment_find(&env, "A", 1)->value.data, "unquoted words") == 0);
    const char *malformed[] = {"export A=x",   "MISSING",   "A=\"unterminated",
                               "A=trailing\\", "A='x'junk", "1A=x"};
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i)
        CHECK(!tired_environment_import(&env, malformed[i], strlen(malformed[i]), &error));
    TiredText encoded = {0};
    TiredEnvironment decoded = {0};
    CHECK(tired_environment_encode(&env, &encoded, &error));
    CHECK(tired_environment_import(&decoded, encoded.data, encoded.length, &error));
    CHECK(decoded.count == env.count);
    for (size_t i = 0; i < env.count; ++i)
    {
        const TiredEnvironmentEntry *entry =
            tired_environment_find(&decoded, env.items[i].name.data, env.items[i].name.length);
        CHECK(entry != NULL && entry->value.length == env.items[i].value.length);
        CHECK(memcmp(entry->value.data, env.items[i].value.data, entry->value.length) == 0);
    }
    TiredText second = {0};
    CHECK(tired_environment_encode(&decoded, &second, &error));
    CHECK(second.length == encoded.length && memcmp(second.data, encoded.data, second.length) == 0);
    tired_text_destroy(&encoded);
    tired_text_destroy(&second);
    tired_environment_destroy(&decoded);
    CHECK(!tired_credentials_add(&credentials, "token=/another", 14, &error));
    CHECK(!tired_credentials_add(&credentials, "other=relative", 14, &error));
    CHECK(!tired_credentials_add(&credentials, "../token=/path", 14, &error));
    CHECK(credentials.count == 1);
    tired_environment_destroy(&env);
    tired_environment_destroy(&env);
    tired_credentials_destroy(&credentials);
    tired_credentials_destroy(&credentials);
    return 0;
}
