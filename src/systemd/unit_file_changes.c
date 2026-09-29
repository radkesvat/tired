#include "tired/unit_file_changes.h"
#include "tired/capture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
void tired_unit_file_changes_destroy(TiredUnitFileChanges *changes)
{
    if (changes == NULL)
        return;
    for (size_t i = 0; i < changes->count; ++i)
    {
        tired_text_destroy(&changes->items[i].type);
        tired_text_destroy(&changes->items[i].path);
        tired_text_destroy(&changes->items[i].source);
    }
    free(changes->items);
    *changes = (TiredUnitFileChanges){0};
}
static bool invalid(TiredError *error)
{
    return tired_error_set(error, TIRED_INVALID, "unit-files-protocol",
                           "Manager returned invalid or excessive unit-file changes.", 0);
}
bool tired_unit_file_changes_read(sd_bus_message *message, bool enable,
                                  TiredUnitFileChanges *output, TiredError *error)
{
    assert(message != NULL && output != NULL);
    TiredUnitFileChanges changes = {0};
    bool ok = false;
    size_t bytes = 0;
    if (sd_bus_message_has_signature(message, enable ? "ba(sss)" : "a(sss)") <= 0)
        return invalid(error);
    if (enable)
    {
        int info;
        if (sd_bus_message_read(message, "b", &info) <= 0)
            return invalid(error);
        changes.install_info_known = true;
        changes.carries_install_info = info != 0;
    }
    if (sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "(sss)") <= 0)
        return invalid(error);
    for (;;)
    {
        int rc = sd_bus_message_enter_container(message, SD_BUS_TYPE_STRUCT, "sss");
        if (rc < 0)
            goto bad;
        if (rc == 0)
            break;
        if (changes.count == 256)
            goto bad;
        const char *type = NULL, *path = NULL, *source = NULL;
        if (sd_bus_message_read(message, "sss", &type, &path, &source) <= 0 ||
            sd_bus_message_exit_container(message) < 0)
            goto bad;
        size_t a = strnlen(type, 65), b = strnlen(path, 4097), c = strnlen(source, 4097);
        if (a == 0 || a > 64 || b == 0 || b > 4096 || c > 4096 || path[0] != '/' ||
            a + b + c + 3 > TIRED_INPUT_LIMIT - bytes ||
            !tired_validate_text(type, a, true, error) ||
            !tired_validate_text(path, b, true, error) ||
            !tired_validate_text(source, c, true, error))
            goto bad;
        TiredUnitFileChange *items = realloc(changes.items, (changes.count + 1) * sizeof(*items));
        if (items == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation",
                            "Cannot allocate unit-file changes.", 0);
            goto done;
        }
        changes.items = items;
        TiredUnitFileChange *item = &changes.items[changes.count++];
        *item = (TiredUnitFileChange){0};
        if (!tired_text_set(&item->type, type, a, 64, error) ||
            !tired_text_set(&item->path, path, b, 4096, error) ||
            !tired_text_set(&item->source, source, c, 4096, error))
            goto done;
        bytes += a + b + c + 3;
    }
    if (sd_bus_message_exit_container(message) < 0 || sd_bus_message_at_end(message, true) <= 0)
        goto bad;
    tired_unit_file_changes_destroy(output);
    *output = changes;
    changes = (TiredUnitFileChanges){0};
    tired_error_clear(error);
    ok = true;
    goto done;
bad:
    invalid(error);
done:
    tired_unit_file_changes_destroy(&changes);
    return ok;
}
