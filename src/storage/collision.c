#include "tired/collision.h"
#include "tired/capture.h"
#include "tired/io.h"
#include "tired/name.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void tired_collision_destroy(TiredCollision *collision)
{
    if (collision == NULL)
        return;
    tired_text_destroy(&collision->path);
    *collision = (TiredCollision){0};
}
static bool valid_name(const TiredText *name, TiredError *error)
{
    if (name->length <= 8 || name->length > TIRED_EXPLICIT_NAME_LIMIT + 8 ||
        memcmp(name->data + name->length - 8, ".service", 8) != 0)
        return tired_error_set(error, TIRED_INVALID, "collision-name",
                               "Expected a full managed service name.", 0);
    return tired_name_validate_base(name->data, name->length - 8, error);
}
static bool io_error(TiredError *error, int number)
{
    return tired_error_set(
        error, number == EACCES || number == EPERM ? TIRED_AUTHORIZATION : TIRED_INVALID,
        "collision-inspection", "Cannot inspect a unit load location; availability is unknown.",
        number);
}
bool tired_collision_check(const TiredText *unit_name, const TiredUnitQueryResult *manager,
                           const TiredTextList *directories, const TiredTextList *pending_names,
                           TiredCollision *collision, TiredError *error)
{
    assert(unit_name != NULL && manager != NULL && directories != NULL && pending_names != NULL &&
           collision != NULL);
    if (!valid_name(unit_name, error))
        return false;
    if (!manager->done || manager->error.status != TIRED_OK || manager->unit_name == NULL ||
        strlen(manager->unit_name) != unit_name->length ||
        memcmp(manager->unit_name, unit_name->data, unit_name->length) != 0)
        return tired_error_set(
            error, TIRED_INVALID, "collision-manager",
            "Collision check requires a successful query for the exact unit name.", 0);
    if (directories->count == 0 || directories->count > 256 || pending_names->count > 4096)
        return tired_error_set(error, TIRED_INVALID, "collision-count",
                               "Collision inventory is absent or exceeds its count limit.", 0);
    size_t bytes = 0;
    for (size_t i = 0; i < directories->count; ++i)
    {
        const TiredText *path = &directories->items[i];
        if (path->length == 0 || path->length > 4096 || path->data[0] != '/' ||
            path->length + 1 > TIRED_INPUT_LIMIT - bytes ||
            !tired_validate_text(path->data, path->length, true, error))
            return tired_error_set(error, TIRED_INVALID, "collision-location",
                                   "Unit locations must be bounded absolute paths.", 0);
        bytes += path->length + 1;
    }
    for (size_t i = 0; i < pending_names->count; ++i)
        if (!valid_name(&pending_names->items[i], error))
            return false;
    TiredCollision found = {0};
    if (manager->file_found)
        found.kind = TIRED_COLLISION_MANAGER_FILE;
    else if (manager->object_found)
        found.kind = TIRED_COLLISION_MANAGER_OBJECT;
    for (size_t i = 0; found.kind == TIRED_COLLISION_NONE && i < pending_names->count; ++i)
        if (pending_names->items[i].length == unit_name->length &&
            memcmp(pending_names->items[i].data, unit_name->data, unit_name->length) == 0)
            found.kind = TIRED_COLLISION_TRANSACTION;
    char entry[TIRED_EXPLICIT_NAME_LIMIT + 16];
    memcpy(entry, unit_name->data, unit_name->length);
    entry[unit_name->length] = '\0';
    for (size_t i = 0; found.kind == TIRED_COLLISION_NONE && i < directories->count; ++i)
    {
        const TiredText *directory = &directories->items[i];
        int fd = open(directory->data, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
        {
            if (errno == ENOENT)
                continue;
            return io_error(error, errno);
        }
        for (unsigned variant = 0; variant < 2; ++variant)
        {
            if (variant == 1)
                memcpy(entry + unit_name->length, ".d", 3);
            else
                entry[unit_name->length] = '\0';
            struct stat status;
            if (fstatat(fd, entry, &status, AT_SYMLINK_NOFOLLOW) == 0)
            {
                found.kind = variant == 0 ? TIRED_COLLISION_UNIT_ENTRY : TIRED_COLLISION_DROP_IN;
                if (!tired_path_absolute(directory, entry, strlen(entry), &found.path, error))
                {
                    (void)close(fd);
                    tired_collision_destroy(&found);
                    return false;
                }
                break;
            }
            if (errno != ENOENT)
            {
                int saved = errno;
                (void)close(fd);
                return io_error(error, saved);
            }
        }
        (void)close(fd);
    }
    tired_collision_destroy(collision);
    *collision = found;
    tired_error_clear(error);
    return true;
}
