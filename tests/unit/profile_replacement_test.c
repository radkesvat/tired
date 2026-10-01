#include "tired/io.h"
#include "tired/mutation.h"
#include "tired/process.h"
#include "tired/profile_frontend.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s [%s]\n", __FILE__, __LINE__, #expression,                   \
                    error.code == NULL ? "none" : error.code);                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    int result = 1, original = -1;
    char fixture[] = "replacement-test-XXXXXX";
    char *created = NULL, *cwd = NULL;
    const char *old_xdg = getenv("XDG_CONFIG_HOME");
    char *saved_xdg = old_xdg == NULL ? NULL : strdup(old_xdg);
    TiredText directory = {0}, path = {0}, source = {0}, output = {0}, executable = {0};
    TiredProcess *process = NULL;
    TiredText installed = {0};
    struct json_object *failure = NULL, *status = NULL;
    TiredProfileCatalog catalog = {0}, layered = {0};
    TiredPlan plan = {0};
    TiredProfileContext context = {.systemd_version = 249};
    TiredRequest request = {.command = TIRED_COMMAND_PROFILES, .yes = true, .json = true};
    TiredError error = {0};
    struct json_object *document = NULL, *recommendations = NULL, *list = NULL, *entries = NULL;
    const TiredProfileEntry *selected = NULL;
    size_t count = 0;
    char bundled_digest[65], replacement_digest[65];
    CHECK(argc == 3 && (old_xdg == NULL || saved_xdg != NULL));
    bool builtin = strcmp(argv[1], "builtin:") == 0;
    CHECK(builtin ? tired_catalog_add_embedded(&catalog, &error)
                  : tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(),
                                                false, &error));
    CHECK(tired_text_set(&executable, "/opt/backhaul", 13, 256, &error));
    CHECK(tired_catalog_select(&catalog, &executable, "auto", true, &selected, &count, &error));
    CHECK(count == 1 && selected->origin == TIRED_PROFILE_BUNDLED);
    memcpy(bundled_digest, selected->digest, sizeof(bundled_digest));
    if (builtin)
    {
        const char *json =
            json_object_to_json_string_ext(selected->profile.document, JSON_C_TO_STRING_PLAIN);
        CHECK(json != NULL &&
              tired_text_set(&source, json, strlen(json), TIRED_PROFILE_LIMIT, &error));
    }
    else
        CHECK(tired_read_file(selected->path.data, TIRED_PROFILE_LIMIT, &source, &error));
    CHECK(tired_json_parse(source.data, source.length, TIRED_PROFILE_LIMIT, &document, &error));
    CHECK(json_object_object_get_ex(document, "recommendations", &recommendations));
    CHECK(json_object_object_add(json_object_array_get_idx(recommendations, 1), "value",
                                 json_object_new_string("7s")) == 0);
    original = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(original >= 0);
    cwd = getcwd(NULL, 0);
    CHECK(cwd != NULL);
    created = mkdtemp(fixture);
    CHECK(created != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &directory, &error));
    CHECK(tired_path_absolute(&directory, "replacement.json", 16, &path, &error));
    CHECK(setenv("XDG_CONFIG_HOME", directory.data, 1) == 0);
    CHECK(tired_spec_set(&request.overrides, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true,
                         &error));
    CHECK(tired_text_list_append(&request.arguments, "install", 7, 2, 4096, &error));
    CHECK(tired_text_list_append(&request.arguments, path.data, path.length, 2, 4096, &error));
    const char *json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_write_private_new(path.data, json, strlen(json), &error));
    CHECK(!tired_profiles_mutate(&request, argv[1], &output, &error));
    CHECK(strcmp(error.code, "profile-replacement") == 0);
    CHECK(unlink(path.data) == 0);
    CHECK(json_object_object_add(document, "replaces", json_object_new_string("backhaul")) == 0);
    json = json_object_to_json_string_ext(document, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_write_private_new(path.data, json, strlen(json), &error));
    CHECK(tired_digest_bytes(json, strlen(json), replacement_digest, &error));
    CHECK(tired_profiles_mutate(&request, argv[1], &output, &error));
    CHECK(tired_profiles_discover(argv[1], true, &catalog, &error));
    CHECK(tired_catalog_select(&catalog, &executable, "auto", true, &selected, &count, &error));
    CHECK(count == 1 && selected->origin == TIRED_PROFILE_USER);
    CHECK(strcmp(selected->digest, replacement_digest) == 0 &&
          strcmp(selected->digest, bundled_digest) != 0);
    CHECK(strstr(selected->path.data, "/tired/profiles.d/backhaul.json") != NULL);
    CHECK(
        tired_catalog_select(&catalog, &executable, "backhaul", false, &selected, &count, &error));
    CHECK(count == 1 && selected->origin == TIRED_PROFILE_BUNDLED);
    CHECK(tired_spec_defaults(&plan.spec, &error));
    CHECK(
        tired_spec_set(&plan.spec, TIRED_FIELD_SCOPE, "user", 4, TIRED_ORIGIN_USER, true, &error));
    CHECK(tired_text_set(&plan.invocation.executable, executable.data, executable.length, 256,
                         &error));
    CHECK(tired_plan_apply_profiles(&plan, &catalog, "auto", &context, &error));
    CHECK(plan.profile_origin == TIRED_PROFILE_USER &&
          plan.spec.fields[TIRED_FIELD_RESTART_SEC].value.microseconds == 7000000);
    tired_text_list_destroy(&request.arguments);
    CHECK(tired_text_list_append(&request.arguments, "list", 4, 2, 4096, &error));
    CHECK(tired_profiles_command(&request, argv[1], &output, &error));
    CHECK(tired_json_parse(output.data, output.length, TIRED_INPUT_LIMIT, &list, &error));
    CHECK(json_object_object_get_ex(list, "profiles", &entries));
    size_t backhaul_count = 0;
    for (size_t i = 0; i < json_object_array_length(entries); ++i)
    {
        struct json_object *entry = json_object_array_get_idx(entries, i), *id = NULL,
                           *origin = NULL;
        CHECK(json_object_object_get_ex(entry, "id", &id));
        if (strcmp(json_object_get_string(id), "backhaul") == 0)
        {
            ++backhaul_count;
            CHECK(json_object_object_get_ex(entry, "origin", &origin) &&
                  strcmp(json_object_get_string(origin), "user") == 0);
        }
    }
    CHECK(backhaul_count == 1);
    CHECK(builtin ? tired_catalog_add_embedded(&layered, &error)
                  : tired_catalog_add_directory(&layered, argv[1], TIRED_PROFILE_BUNDLED, getuid(),
                                                false, &error));
    size_t bundled_count = layered.count;
    CHECK(tired_catalog_add_directory(&layered, directory.data, TIRED_PROFILE_ADMIN, getuid(),
                                      false, &error));
    CHECK(tired_catalog_add_directory(&layered, directory.data, TIRED_PROFILE_USER, getuid(), false,
                                      &error));
    CHECK(tired_catalog_select(&layered, &executable, "auto", false, &selected, &count, &error));
    CHECK(count == 1 && selected->origin == TIRED_PROFILE_ADMIN);
    CHECK(tired_catalog_select(&layered, &executable, "auto", true, &selected, &count, &error));
    CHECK(count == 1 && selected->origin == TIRED_PROFILE_USER);
    CHECK(!tired_catalog_add_directory(&layered, directory.data, TIRED_PROFILE_USER, getuid(),
                                       false, &error));
    CHECK(layered.count == bundled_count + 2);
    tired_text_list_destroy(&request.arguments);
    CHECK(tired_text_list_append(&request.arguments, "remove", 6, 2, 4096, &error));
    CHECK(tired_text_list_append(&request.arguments, "backhaul", 8, 2, 4096, &error));
    CHECK(tired_path_absolute(&directory, "tired/profiles.d/backhaul.json",
                              strlen("tired/profiles.d/backhaul.json"), &installed, &error));
    CHECK(chmod(installed.data, 0666) == 0);
    tired_error_clear(&error);
    CHECK(!tired_profiles_mutate(&request, argv[1], &output, &error));
    CHECK(error.status == TIRED_AUTHORIZATION && strcmp(error.code, "profile-trust") == 0);
    char *remove_args[] = {argv[2],  "profiles", "remove", "backhaul",
                           "--user", "--yes",    "--json", NULL};
    CHECK(tired_process_start(argv[2], remove_args, environ, 5000, 65536, &process, &error));
    while (!tired_process_step(process))
    {
        const struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL);
    }
    TiredProcessResult removed = tired_process_result(process);
    CHECK(removed.outcome == TIRED_PROCESS_EXITED && removed.exit_code == TIRED_AUTHORIZATION);
    CHECK(tired_json_parse(removed.standard_output, removed.output_length, TIRED_INPUT_LIMIT,
                           &failure, &error));
    CHECK(json_object_object_get_ex(failure, "exit_code", &status));
    CHECK(json_object_get_int(status) == removed.exit_code);
    CHECK(json_object_object_get_ex(failure, "error", &status));
    CHECK(strcmp(json_object_get_string(status), "profile-trust") == 0);
    CHECK(access(installed.data, F_OK) == 0);
    CHECK(chmod(installed.data, 0600) == 0);
    CHECK(tired_profiles_mutate(&request, argv[1], &output, &error));
    CHECK(tired_profiles_discover(argv[1], true, &catalog, &error));
    CHECK(tired_catalog_select(&catalog, &executable, "auto", true, &selected, &count, &error));
    CHECK(count == 1 && selected->origin == TIRED_PROFILE_BUNDLED &&
          strcmp(selected->digest, bundled_digest) == 0);
    CHECK(!tired_profiles_mutate(&request, argv[1], &output, &error));
    CHECK(strcmp(error.code, "local-profile-not-found") == 0);
    result = 0;
cleanup:
    if (created != NULL && original >= 0 && chdir(directory.data) == 0)
    {
        (void)unlink("replacement.json");
        (void)unlink("tired/profiles.d/backhaul.json");
        (void)unlink("tired/operation.lock");
        (void)rmdir("tired/profiles.d");
        (void)rmdir("tired");
        (void)fchdir(original);
        if (rmdir(created) != 0)
            result = 1;
    }
    if (original >= 0)
        (void)close(original);
    if (saved_xdg != NULL)
        (void)setenv("XDG_CONFIG_HOME", saved_xdg, 1);
    else
        (void)unsetenv("XDG_CONFIG_HOME");
    free(saved_xdg);
    free(cwd);
    tired_process_destroy(process);
    tired_text_destroy(&installed);
    json_object_put(failure);
    tired_text_destroy(&directory);
    tired_text_destroy(&path);
    tired_text_destroy(&source);
    tired_text_destroy(&output);
    tired_text_destroy(&executable);
    tired_catalog_destroy(&catalog);
    tired_catalog_destroy(&layered);
    tired_plan_destroy(&plan);
    tired_request_destroy(&request);
    json_object_put(document);
    json_object_put(list);
    return result;
}
