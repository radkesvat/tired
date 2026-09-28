#include "tired/render.h"
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
static bool set(TiredServiceSpec *spec, TiredFieldId id, const char *value)
{
    return tired_spec_set(spec, id, value, strlen(value), TIRED_ORIGIN_USER, true, NULL);
}

int main(int argc, char **argv)
{
    TiredServiceSpec spec = {0};
    TiredText unit = {0}, repeat = {0}, environment = {0};
    TiredCredentials credentials = {0};
    TiredError error = {0};
    const char *uuid = "6d48db39-65c0-4de0-ae2a-c5e0208ea1d7";
    CHECK(tired_spec_defaults(&spec, &error));
    CHECK(!tired_render_unit(&spec, uuid, NULL, NULL, &unit, &error));
    CHECK(set(&spec, TIRED_FIELD_NAME, "tired-render-test"));
    CHECK(set(&spec, TIRED_FIELD_DESCRIPTION, "Literal %n description"));
    CHECK(set(&spec, TIRED_FIELD_EXECUTABLE, "/usr/bin/true"));
    CHECK(set(&spec, TIRED_FIELD_WORKING_DIRECTORY, "/tmp"));
    CHECK(set(&spec, TIRED_FIELD_RUN_AS, "root"));
    CHECK(set(&spec, TIRED_FIELD_GROUP, "root"));
    CHECK(tired_spec_resolve_scope(&spec, &error));
    const char *arguments[] = {"true", "",   ";", "$HOME", "${TOKEN}", "%n", "a\nb",
                               "\\",   "\"", "-", "+",     "!",        ":",  "@"};
    for (size_t i = 0; i < sizeof(arguments) / sizeof(arguments[0]); ++i)
        CHECK(tired_spec_append(&spec, TIRED_FIELD_ARGV, arguments[i], strlen(arguments[i]),
                                TIRED_ORIGIN_CAPTURE, &error));
    CHECK(tired_spec_append(&spec, TIRED_FIELD_ENVIRONMENT_FILES, "/tmp/live env", 13,
                            TIRED_ORIGIN_USER, &error));
    CHECK(tired_text_set(&environment, "/tmp/private env", 16, 1024, &error));
    CHECK(tired_credentials_add(&credentials, "token=/tmp/credential", 21, &error));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_SOFT, "1024"));
    CHECK(set(&spec, TIRED_FIELD_NOFILE_HARD, "4096"));
    CHECK(set(&spec, TIRED_FIELD_CPU_QUOTA, "125.25%"));
    CHECK(set(&spec, TIRED_FIELD_NETWORK, "online"));
    CHECK(tired_render_unit(&spec, uuid, &environment, &credentials, &unit, &error));
    CHECK(
        strstr(
            unit.data,
            "ExecStart=:\"/usr/bin/true\" \"\" \";\" \"$HOME\" \"${TOKEN}\" \"%%n\" \"a\\x0ab\"") !=
        NULL);
    CHECK(strstr(unit.data, "Description=Literal %%n description\n") != NULL);
    CHECK(strstr(unit.data, "LimitNOFILE=1024:4096\n") != NULL);
    CHECK(strstr(unit.data, "CPUQuota=125.25%\n") != NULL);
    CHECK(strstr(unit.data, "After=network-online.target\nWants=network-online.target\n") != NULL);
    const char *external = strstr(unit.data, "EnvironmentFile=/tmp/live env\n");
    const char *managed = strstr(unit.data, "EnvironmentFile=/tmp/private env\n");
    CHECK(external != NULL && managed != NULL && external < managed);
    CHECK(strstr(unit.data, "LoadCredential=token:/tmp/credential\n") != NULL);
    CHECK(tired_render_unit(&spec, uuid, &environment, &credentials, &repeat, &error));
    CHECK(strcmp(unit.data, repeat.data) == 0);
    CHECK(!tired_render_unit(&spec, "injected\n[Service]", NULL, NULL, &unit, &error));
    CHECK(strcmp(unit.data, repeat.data) == 0);
    if (argc == 2)
    {
        FILE *file = fopen(argv[1], "wx");
        CHECK(file != NULL);
        CHECK(fwrite(unit.data, 1, unit.length, file) == unit.length);
        CHECK(fclose(file) == 0);
    }
    CHECK(set(&spec, TIRED_FIELD_SCOPE, "user"));
    CHECK(set(&spec, TIRED_FIELD_NETWORK, "none"));
    CHECK(tired_spec_resolve_scope(&spec, &error));
    CHECK(tired_render_unit(&spec, uuid, NULL, NULL, &unit, &error));
    CHECK(strstr(unit.data, "User=") == NULL && strstr(unit.data, "Group=") == NULL);
    CHECK(strstr(unit.data, "WantedBy=default.target\n") != NULL);
    tired_spec_destroy(&spec);
    tired_text_destroy(&unit);
    tired_text_destroy(&repeat);
    tired_text_destroy(&environment);
    tired_credentials_destroy(&credentials);
    return 0;
}
