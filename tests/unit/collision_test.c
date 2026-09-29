#include "tired/collision.h"
#include "tired/io.h"
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
int main(void)
{
    int result = 1;
    char fixture[] = "/tmp/tired-collision-XXXXXX";
    char *root = mkdtemp(fixture);
    TiredText directory = {0}, alias = {0}, unit = {0}, dropin = {0};
    TiredTextList locations = {0}, pending = {0};
    TiredCollision collision = {0};
    TiredError error = {0};
    TiredText name = {.data = "relay.service", .length = 13};
    TiredUnitQueryResult manager = {.done = true, .unit_name = "relay.service"};
    CHECK(root != NULL);
    TiredText base = {.data = root, .length = strlen(root)};
    CHECK(tired_path_absolute(&base, "units", 5, &directory, &error));
    CHECK(tired_path_absolute(&base, "alias", 5, &alias, &error));
    CHECK(tired_path_absolute(&directory, name.data, name.length, &unit, &error));
    CHECK(tired_path_absolute(&directory, "relay.service.d", 15, &dropin, &error));
    CHECK(mkdir(directory.data, 0700) == 0 && symlink("units", alias.data) == 0);
    CHECK(tired_text_list_append(&locations, alias.data, alias.length, 256, TIRED_INPUT_LIMIT,
                                 &error));
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_NONE);
    for (unsigned kind = 0; kind < 5; ++kind)
    {
        if (kind == 0)
            CHECK(tired_write_private_new(unit.data, "unit", 4, &error));
        else if (kind == 1)
            CHECK(symlink("/dev/null", unit.data) == 0);
        else if (kind == 2)
            CHECK(symlink("missing", unit.data) == 0);
        else if (kind == 3)
            CHECK(mkfifo(unit.data, 0600) == 0);
        else
            CHECK(mkdir(unit.data, 0700) == 0);
        CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
        CHECK(collision.kind == TIRED_COLLISION_UNIT_ENTRY);
        CHECK(strstr(collision.path.data, "/alias/relay.service") != NULL);
        if (kind == 4)
            CHECK(rmdir(unit.data) == 0);
        else
            CHECK(unlink(unit.data) == 0);
    }
    CHECK(mkdir(dropin.data, 0700) == 0);
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_DROP_IN);
    CHECK(rmdir(dropin.data) == 0);
    CHECK(symlink("missing", dropin.data) == 0);
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_DROP_IN);
    CHECK(unlink(dropin.data) == 0);
    CHECK(unlink(alias.data) == 0);
    CHECK(tired_write_private_new(alias.data, "not a directory", 15, &error));
    CHECK(!tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_DROP_IN &&
          strstr(collision.path.data, ".service.d") != NULL);
    CHECK(unlink(alias.data) == 0);
    /* A now-missing optional load location is empty, not an access failure. */
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_NONE);
    CHECK(
        tired_text_list_append(&pending, name.data, name.length, 4096, TIRED_INPUT_LIMIT, &error));
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_TRANSACTION);
    manager.file_found = true;
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_MANAGER_FILE);
    manager.file_found = false;
    manager.object_found = true;
    CHECK(tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_MANAGER_OBJECT);
    manager.unit_name = "different.service";
    CHECK(!tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    CHECK(collision.kind == TIRED_COLLISION_MANAGER_OBJECT);
    manager.unit_name = name.data;
    manager.done = false;
    CHECK(!tired_collision_check(&name, &manager, &locations, &pending, &collision, &error));
    result = 0;
cleanup:
    if (unit.data != NULL)
    {
        (void)unlink(unit.data);
        (void)rmdir(unit.data);
    }
    if (dropin.data != NULL)
    {
        (void)unlink(dropin.data);
        (void)rmdir(dropin.data);
    }
    if (alias.data != NULL)
        (void)unlink(alias.data);
    if (directory.data != NULL)
        (void)rmdir(directory.data);
    if (root != NULL)
        (void)rmdir(root);
    tired_text_destroy(&directory);
    tired_text_destroy(&alias);
    tired_text_destroy(&unit);
    tired_text_destroy(&dropin);
    tired_text_list_destroy(&locations);
    tired_text_list_destroy(&pending);
    tired_collision_destroy(&collision);
    return result;
}
