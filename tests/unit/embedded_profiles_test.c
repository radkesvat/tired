#include "tired/catalog.h"
#include "tired/payload.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CHECK(e)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(e))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    int result = 1;
    TiredError error = {0};
    TiredProfileCatalog embedded = {0}, disk = {0};
    TiredText path = {0};
    CHECK(argc == 2);
    CHECK(tired_payload_path(false, &path, &error) && strcmp(path.data, "builtin:") == 0);
    CHECK(tired_catalog_add_embedded(&embedded, &error));
    CHECK(tired_catalog_add_directory(&disk, argv[1], TIRED_PROFILE_BUNDLED, getuid(), false,
                                      &error));
    CHECK(embedded.count == disk.count && embedded.count > 0);
    for (size_t i = 0; i < disk.count; ++i)
    {
        CHECK(strcmp(embedded.items[i].profile.id, disk.items[i].profile.id) == 0);
        CHECK(strcmp(embedded.items[i].digest, disk.items[i].digest) == 0);
        CHECK(
            json_object_equal(embedded.items[i].profile.document, disk.items[i].profile.document));
        CHECK(strncmp(embedded.items[i].path.data, "builtin:", 8) == 0);
    }
    CHECK(!tired_catalog_add_embedded(&embedded, &error) &&
          strcmp(error.code, "profile-duplicate") == 0 && embedded.count == disk.count);
    result = 0;
cleanup:
    tired_text_destroy(&path);
    tired_catalog_destroy(&embedded);
    tired_catalog_destroy(&disk);
    return result;
}
