#include "tired/catalog.h"
#include "tired/io.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    int result = 1, original = -1;
    char fixture[] = "catalog-test-XXXXXX";
    char *created = NULL, *cwd = NULL;
    TiredProfileCatalog catalog = {0}, local = {0};
    TiredError error = {0};
    TiredText executable = {0}, directory = {0}, path = {0}, source = {0};
    struct json_object *doc = NULL;
    const TiredProfileEntry *selected = NULL;
    size_t matches = 0;
    CHECK(argc == 2);
    CHECK(tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                      &error));
    CHECK(catalog.count == 4 && strlen(catalog.items[0].digest) == 64);
    CHECK(tired_text_set(&executable, "/opt/backhaul", 13, 256, &error));
    CHECK(tired_catalog_select(&catalog, &executable, "auto", false, &selected, &matches, &error));
    CHECK(matches == 1 && selected != NULL && strcmp(selected->profile.id, "backhaul") == 0);
    CHECK(tired_catalog_select(&catalog, &executable, "frpc", false, &selected, &matches, &error));
    CHECK(matches == 1 && strcmp(selected->profile.id, "frpc") == 0);
    CHECK(!tired_catalog_select(&catalog, &executable, "missing", false, &selected, &matches,
                                &error));
    CHECK(
        tired_catalog_add_directory(&local, argv[1], TIRED_PROFILE_USER, getuid(), false, &error));
    CHECK(tired_catalog_select(&local, &executable, "auto", false, &selected, &matches, &error));
    CHECK(selected == NULL && matches == 0);
    tired_catalog_destroy(&local);
    CHECK(!tired_catalog_add_directory(&catalog, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                       &error));
    CHECK(catalog.count == 4);
    original = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(original >= 0);
    cwd = getcwd(NULL, 0);
    CHECK(cwd != NULL);
    created = mkdtemp(fixture);
    CHECK(created != NULL);
    TiredText base = {.data = cwd, .length = strlen(cwd)};
    CHECK(tired_path_absolute(&base, created, strlen(created), &directory, &error));
    CHECK(tired_catalog_add_directory(&local, directory.data, TIRED_PROFILE_USER, getuid(), false,
                                      &error));
    CHECK(chmod(created, 0777) == 0);
    CHECK(!tired_catalog_add_directory(&local, directory.data, TIRED_PROFILE_USER, getuid(), false,
                                       &error));
    CHECK(chmod(created, 0700) == 0);
    CHECK(chdir(created) == 0);
    CHECK(symlink(catalog.items[0].path.data, "linked.json") == 0);
    CHECK(!tired_catalog_add_directory(&local, directory.data, TIRED_PROFILE_USER, getuid(), false,
                                       &error));
    CHECK(unlink("linked.json") == 0);
    CHECK(tired_read_file(catalog.items[0].path.data, TIRED_PROFILE_LIMIT, &source, &error));
    CHECK(tired_write_private_new("first.json", source.data, source.length, &error));
    CHECK(link("first.json", "second.json") == 0);
    CHECK(!tired_catalog_add_directory(&local, directory.data, TIRED_PROFILE_USER, getuid(), false,
                                       &error));
    CHECK(unlink("second.json") == 0 && unlink("first.json") == 0);
    CHECK(tired_json_parse(source.data, source.length, TIRED_PROFILE_LIMIT, &doc, &error));
    CHECK(json_object_object_add(doc, "id", json_object_new_string("alternate")) == 0);
    const char *json = json_object_to_json_string_ext(doc, JSON_C_TO_STRING_PLAIN);
    CHECK(tired_write_private_new("alternate.json", json, strlen(json), &error));
    CHECK(tired_catalog_add_directory(&catalog, directory.data, TIRED_PROFILE_ADMIN, getuid(),
                                      false, &error));
    CHECK(tired_catalog_select(&catalog, &executable, "auto", false, &selected, &matches, &error));
    CHECK(matches == 2 && selected == NULL);
    CHECK(tired_path_absolute(&directory, "missing", 7, &path, &error));
    CHECK(
        tired_catalog_add_directory(&local, path.data, TIRED_PROFILE_USER, getuid(), true, &error));
    result = 0;
cleanup:
    if (created != NULL && original >= 0)
    {
        if (fchdir(original) == 0 && chdir(created) == 0)
        {
            (void)unlink("linked.json");
            (void)unlink("first.json");
            (void)unlink("second.json");
            (void)unlink("alternate.json");
        }
        (void)fchdir(original);
        if (rmdir(created) != 0)
            result = 1;
    }
    if (original >= 0)
        (void)close(original);
    free(cwd);
    json_object_put(doc);
    tired_text_destroy(&executable);
    tired_text_destroy(&directory);
    tired_text_destroy(&path);
    tired_text_destroy(&source);
    tired_catalog_destroy(&catalog);
    tired_catalog_destroy(&local);
    return result;
}
