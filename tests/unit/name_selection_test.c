#include "tired/name_selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
int main(void)
{
    int result = 1;
    char fixture[] = "/tmp/tired-name-selection-XXXXXX";
    char *root = mkdtemp(fixture);
    char path[256] = {0};
    TiredText base = {.data = "relay", .length = 5};
    TiredTextList directories = {0}, pending = {0};
    TiredNameSelection *selection = NULL;
    TiredError error = {0};
    bool available = true;
    TiredUnitQueryResult manager = {.done = true, .unit_name = "relay.service"};
    CHECK(root != NULL);
    CHECK(tired_text_list_append(&directories, root, strlen(root), 256, TIRED_INPUT_LIMIT, &error));
    CHECK(tired_text_list_append(&pending, "relay-3.service", 15, 4096, TIRED_INPUT_LIMIT, &error));
    CHECK(snprintf(path, sizeof(path), "%s/relay-4.service", root) > 0);
    CHECK(symlink("missing", path) == 0);
    CHECK(tired_name_selection_start(&base, false, &directories, &pending, &selection, &error));
    /* Selection owns the snapshot, independently of caller list lifetime. */
    tired_text_list_destroy(&pending);
    manager.done = false;
    CHECK(!tired_name_selection_observe(selection, &manager, &available, &error));
    CHECK(available &&
          strcmp(tired_name_selection_candidate(selection)->data, "relay.service") == 0);
    manager.done = true;
    manager.file_found = true;
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && !available);
    CHECK(strcmp(tired_name_selection_candidate(selection)->data, "relay-2.service") == 0);
    /* A stale observation must not accidentally approve the next candidate. */
    CHECK(!tired_name_selection_observe(selection, &manager, &available, &error));
    manager.unit_name = "relay-2.service";
    manager.file_found = false;
    manager.object_found = true;
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && !available);
    manager.object_found = false;
    manager.unit_name = "relay-3.service";
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && !available);
    manager.unit_name = "relay-4.service";
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && !available);
    manager.unit_name = "relay-5.service";
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && available);
    CHECK(strcmp(tired_name_selection_candidate(selection)->data, "relay-5.service") == 0);
    /* Rechecking the same tentative candidate can discover a later collision. */
    manager.file_found = true;
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && !available);
    CHECK(strcmp(tired_name_selection_candidate(selection)->data, "relay-6.service") == 0);
    tired_name_selection_destroy(selection);
    selection = NULL;
    CHECK(tired_name_selection_start(&base, true, &directories, &pending, &selection, &error));
    manager.unit_name = "relay.service";
    CHECK(!tired_name_selection_observe(selection, &manager, &available, &error));
    CHECK(error.status == TIRED_CONFLICT);
    CHECK(strcmp(tired_name_selection_candidate(selection)->data, "relay.service") == 0);
    manager.file_found = false;
    CHECK(tired_name_selection_observe(selection, &manager, &available, &error) && available);
    tired_name_selection_destroy(selection);
    selection = NULL;
    /* A non-directory load location cannot prove availability. */
    TiredTextList bad_locations = {.items = &(TiredText){.data = path, .length = strlen(path)},
                                   .count = 1};
    CHECK(unlink(path) == 0 && symlink("/dev/null", path) == 0);
    CHECK(tired_name_selection_start(&base, false, &bad_locations, &pending, &selection, &error));
    CHECK(!tired_name_selection_observe(selection, &manager, &available, &error));
    CHECK(available &&
          strcmp(tired_name_selection_candidate(selection)->data, "relay.service") == 0);
    tired_name_selection_destroy(selection);
    selection = NULL;
    char long_base[82];
    memset(long_base, 'a', 81);
    long_base[81] = '\0';
    TiredText oversized = {.data = long_base, .length = 81};
    CHECK(
        !tired_name_selection_start(&oversized, false, &directories, &pending, &selection, &error));
    CHECK(selection == NULL);
    CHECK(tired_name_selection_start(&oversized, true, &directories, &pending, &selection, &error));
    tired_name_selection_destroy(selection);
    selection = NULL;
    tired_text_list_destroy(&directories);
    CHECK(!tired_name_selection_start(&base, false, &directories, &pending, &selection, &error));
    CHECK(selection == NULL);
    result = 0;
cleanup:
    tired_name_selection_destroy(selection);
    tired_text_list_destroy(&directories);
    tired_text_list_destroy(&pending);
    if (path[0] != '\0')
        (void)unlink(path);
    if (root != NULL)
        (void)rmdir(root);
    return result;
}
