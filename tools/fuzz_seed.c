#include "tired/encode.h"
#include "tired/io.h"
#include "tired/mutation.h"
#include <stdio.h>
#include <string.h>
static bool seed(const char *directory, const char *name, unsigned char kind,
                 const TiredText *bytes, TiredError *error)
{
    TiredText path = {0};
    TiredText root = {.data = (char *)directory, .length = strlen(directory)};
    TiredBuffer buffer;
    tired_buffer_init(&buffer, TIRED_INPUT_LIMIT + 1);
    bool ok = tired_path_absolute(&root, name, strlen(name), &path, error) &&
              tired_buffer_append(&buffer, (const char *)&kind, 1, error) &&
              tired_buffer_append(&buffer, bytes->data, bytes->length, error) &&
              tired_write_private_new(path.data, buffer.data, buffer.length, error);
    tired_text_destroy(&path);
    tired_buffer_destroy(&buffer);
    return ok;
}
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fputs("Usage: tired_fuzz_seed ABSOLUTE_NEW_CORPUS_DIRECTORY SOURCE_DIRECTORY\n", stderr);
        return 2;
    }
    TiredError error = {0};
    TiredText bytes = {0}, path = {0};
    TiredLayout layout = {0};
    TiredMutation mutation = {0};
    TiredPlan plan = {0};
    TiredRequest request = {0};
    TiredSettings settings = {0};
    TiredTextList risks = {0};
    TiredDirectory *directory = NULL;
    TiredText source = {.data = argv[2], .length = strlen(argv[2])};
    int result = 1;
    if (!tired_directory_ensure(argv[1], true, &directory, &error))
        goto done;
    const char *profiles[] = {"generic", "backhaul", "frpc", "frps"};
    for (size_t i = 0; i < 4; ++i)
    {
        char relative[64];
        snprintf(relative, sizeof(relative), "profiles/%s.json", profiles[i]);
        if (!tired_path_absolute(&source, relative, strlen(relative), &path, &error) ||
            !tired_read_file(path.data, TIRED_INPUT_LIMIT, &bytes, &error) ||
            !seed(argv[1], profiles[i], 0, &bytes, &error))
            goto done;
    }
    if (!tired_path_absolute(&source, "defaults.json", 13, &path, &error) ||
        !tired_read_file(path.data, TIRED_INPUT_LIMIT, &bytes, &error) ||
        !seed(argv[1], "settings", 1, &bytes, &error))
        goto done;
    const char *arguments[] = {"tired",         "--user", "--profile", "none", "--",
                               "/usr/bin/true", "",       "$literal",  "%n",   "\n"};
    tired_settings_defaults(&settings);
    if (!tired_cli_parse(10, arguments, &request, &error) ||
        !tired_plan_prepare(&request, &plan, &error) ||
        !tired_layout_resolve(true, "/fixture", "/fixture/config", "/fixture/state", "/fixture/run",
                              &layout, &error) ||
        !tired_mutation_from_plan(&plan, &layout, &settings, &risks, &mutation, &error) ||
        !tired_mutation_encode(&mutation, &bytes, &error) ||
        !seed(argv[1], "mutation", 2, &bytes, &error) ||
        !tired_service_record_encode(&mutation.proposed, &bytes, &error) ||
        !seed(argv[1], "record", 8, &bytes, &error) ||
        !tired_spec_encode(&mutation.proposed.spec, &bytes, &error) ||
        !seed(argv[1], "model", 6, &bytes, &error))
        goto done;
    const char *texts[] = {"A='literal $HOME'\nB=\"with spaces\"\n", "example.service",
                           "\"'\\\t\n$%", "{\"literal\":\"data\",\"list\":[1,null,true]}"};
    const char *names[] = {"environment", "name", "token", "json"};
    const unsigned char kinds[] = {3, 4, 5, 7};
    for (size_t i = 0; i < 4; ++i)
    {
        TiredText text = {.data = (char *)texts[i], .length = strlen(texts[i])};
        if (!seed(argv[1], names[i], kinds[i], &text, &error))
            goto done;
    }
    const char cli[] = "--user\0--json\0--profile\0none\0--\0/usr/bin/true\0\0$literal\0%n\0";
    TiredText cli_text = {.data = (char *)cli, .length = sizeof(cli) - 1};
    if (!seed(argv[1], "cli", 9, &cli_text, &error))
        goto done;
    result = 0;
done:
    if (result)
        fprintf(stderr, "Cannot prepare corpus: %s\n", error.code == NULL ? "unknown" : error.code);
    tired_directory_destroy(directory);
    tired_text_destroy(&bytes);
    tired_text_destroy(&path);
    tired_text_list_destroy(&risks);
    tired_mutation_destroy(&mutation);
    tired_layout_destroy(&layout);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    tired_settings_destroy(&settings);
    return result;
}
