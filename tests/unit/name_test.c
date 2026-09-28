#include "tired/name.h"

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

static int check_hint(const char *a, const char *b, const char *c, const char *expected,
                      TiredNameBasis expected_basis)
{
    TiredTextList argv = {0};
    TiredText name = {0};
    TiredError error = {0};
    TiredNameBasis basis = TIRED_NAME_EXECUTABLE;
    CHECK(tired_text_list_append(&argv, a, strlen(a), 3, 1024, &error));
    if (b != NULL)
        CHECK(tired_text_list_append(&argv, b, strlen(b), 3, 1024, &error));
    if (c != NULL)
        CHECK(tired_text_list_append(&argv, c, strlen(c), 3, 1024, &error));
    CHECK(tired_name_suggest(&argv, &name, &basis, &error));
    CHECK(strcmp(name.data, expected) == 0 && basis == expected_basis);
    CHECK(strcmp(argv.items[0].data, a) == 0);
    if (b != NULL)
        CHECK(strcmp(argv.items[1].data, b) == 0);
    if (c != NULL)
        CHECK(strcmp(argv.items[2].data, c) == 0);
    tired_text_destroy(&name);
    tired_text_list_destroy(&argv);
    return 0;
}

int main(void)
{
    CHECK(check_hint("./Program", NULL, NULL, "program", TIRED_NAME_EXECUTABLE) == 0);
    CHECK(check_hint("python3", "bot.py", NULL, "bot", TIRED_NAME_SCRIPT) == 0);
    CHECK(check_hint("/venv/bin/python3.12", "-m", "package.worker", "package-worker",
                     TIRED_NAME_MODULE) == 0);
    CHECK(check_hint("node", "/tmp/server.js", NULL, "server", TIRED_NAME_SCRIPT) == 0);
    CHECK(check_hint("bash", "worker.sh", NULL, "worker", TIRED_NAME_SCRIPT) == 0);
    CHECK(check_hint("java", "-jar", "/opt/relay.jar", "relay", TIRED_NAME_SCRIPT) == 0);
    CHECK(check_hint("sh", "-c", "rm /anything", "sh", TIRED_NAME_WRAPPER) == 0);
    CHECK(check_hint("env", "python3", "bot.py", "env", TIRED_NAME_WRAPPER) == 0);
    CHECK(check_hint("sudo", "./server", NULL, "sudo", TIRED_NAME_WRAPPER) == 0);
    CHECK(check_hint("nohup", "./server", NULL, "nohup", TIRED_NAME_WRAPPER) == 0);
    CHECK(check_hint("tmux", "./server", NULL, "tmux", TIRED_NAME_WRAPPER) == 0);
    CHECK(check_hint("pythonish", "bot.py", NULL, "pythonish", TIRED_NAME_EXECUTABLE) == 0);
    CHECK(check_hint("python3", "-c", "print(1)", "python3", TIRED_NAME_EXECUTABLE) == 0);
    CHECK(check_hint("-- Hello $$ World --", NULL, NULL, "hello-world", TIRED_NAME_EXECUTABLE) ==
          0);
    CHECK(check_hint("...", NULL, NULL, "service", TIRED_NAME_EXECUTABLE) == 0);
    CHECK(check_hint("a..b", NULL, NULL, "a.-b", TIRED_NAME_EXECUTABLE) == 0);
    CHECK(check_hint("é", NULL, NULL, "service", TIRED_NAME_EXECUTABLE) == 0);
    TiredText name = {0};
    TiredText unit = {0};
    TiredError error = {0};
    CHECK(tired_name_explicit("Relay.service", 13, &name, &error));
    CHECK(strcmp(name.data, "Relay") == 0);
    const char *bad[] = {"",     ".service", "../x", "foo/bar", "foo@bar", "-foo",
                         "foo_", "a..b",     "a\nb", "a b",     "é"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(!tired_name_explicit(bad[i], strlen(bad[i]), &name, &error));
    CHECK(!tired_name_explicit("a\0b", 3, &name, &error));
    CHECK(strcmp(name.data, "Relay") == 0);
    CHECK(tired_name_candidate(&name, 1, &unit, &error));
    CHECK(strcmp(unit.data, "Relay.service") == 0);
    CHECK(tired_name_candidate(&name, 2, &unit, &error));
    CHECK(strcmp(unit.data, "Relay-2.service") == 0);
    CHECK(!tired_name_candidate(&name, 0, &unit, &error));
    CHECK(strcmp(unit.data, "Relay-2.service") == 0);
    CHECK(tired_text_set(&name, "foo.service", 11, 200, &error));
    CHECK(tired_name_candidate(&name, 1, &unit, &error));
    CHECK(strcmp(unit.data, "foo.service.service") == 0);
    char long_name[201];
    memset(long_name, 'x', sizeof(long_name));
    CHECK(tired_name_explicit(long_name, 200, &name, &error));
    CHECK(!tired_name_explicit(long_name, 201, &name, &error));
    CHECK(tired_name_candidate(&name, UINT64_MAX, &unit, &error));
    CHECK(unit.length == 229);
    TiredTextList argv = {0};
    TiredNameBasis basis = TIRED_NAME_EXECUTABLE;
    CHECK(!tired_name_suggest(&argv, &name, &basis, &error));
    CHECK(tired_text_list_append(&argv, long_name, 201, 1, 1024, &error));
    CHECK(tired_name_suggest(&argv, &name, &basis, &error));
    CHECK(name.length == 80);
    tired_text_list_destroy(&argv);
    tired_text_destroy(&name);
    tired_text_destroy(&unit);
    return 0;
}
