#include "tired/service_inventory.h"
#include "tired/io.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
void tired_service_inventory_destroy(TiredServiceInventory *inventory)
{
    if (inventory == NULL)
        return;
    for (size_t i = 0; i < inventory->count; ++i)
    {
        tired_text_destroy(&inventory->entries[i].filename);
        tired_service_metadata_destroy(&inventory->entries[i].metadata);
    }
    free(inventory->entries);
    *inventory = (TiredServiceInventory){0};
}
static bool io_error(TiredError *error)
{
    return tired_error_set(error, TIRED_RUNTIME_FAILED, "service-inventory-io",
                           "Cannot inspect service record inventory.", errno);
}
static int compare(const void *left, const void *right)
{
    return strcmp(((const TiredText *)left)->data, ((const TiredText *)right)->data);
}
bool tired_service_inventory_load(const TiredLayout *layout, TiredServiceInventory *output,
                                  TiredError *error)
{
    assert(layout != NULL && output != NULL);
    TiredError local_error = {0};
    if (error == NULL)
        error = &local_error;
    if (layout->paths[TIRED_PATH_RECORDS].data == NULL)
        return tired_error_set(error, TIRED_INVALID, "service-inventory-layout",
                               "Records layout is unset.", 0);
    TiredDirectory *directory = NULL;
    TiredTextList names = {0};
    TiredServiceInventory inventory = {.complete = true};
    DIR *scan = NULL;
    bool ok = false;
    if (!tired_directory_open(layout->paths[TIRED_PATH_RECORDS].data,
                              layout->user_scope ? geteuid() : 0, true, &directory, error))
    {
        if (error->status != TIRED_NOT_FOUND)
            goto done;
        goto success;
    }
    int fd = tired_directory_fd(directory);
    struct stat before, after;
    if (fstat(fd, &before) != 0)
    {
        io_error(error);
        goto done;
    }
    int scan_fd = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (scan_fd < 0)
    {
        io_error(error);
        goto done;
    }
    scan = fdopendir(scan_fd);
    if (scan == NULL)
    {
        io_error(error);
        (void)close(scan_fd);
        goto done;
    }
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(scan);
        if (entry == NULL)
        {
            if (errno != 0)
            {
                io_error(error);
                goto done;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!tired_text_list_append(&names, entry->d_name, strlen(entry->d_name), 1024,
                                    TIRED_INPUT_LIMIT, error))
            goto done;
    }
    if (closedir(scan) != 0)
    {
        scan = NULL;
        io_error(error);
        goto done;
    }
    scan = NULL;
    if (names.count != 0)
    {
        qsort(names.items, names.count, sizeof(*names.items), compare);
        inventory.entries = calloc(names.count, sizeof(*inventory.entries));
        if (inventory.entries == NULL)
        {
            tired_error_set(error, TIRED_INTERNAL, "allocation",
                            "Cannot allocate service inventory.", 0);
            goto done;
        }
    }
    inventory.count = names.count;
    size_t budget = 64U * TIRED_INPUT_LIMIT;
    for (size_t i = 0; i < names.count; ++i)
    {
        TiredServiceInventoryEntry *entry = &inventory.entries[i];
        entry->filename = names.items[i];
        names.items[i] = (TiredText){0};
        if (entry->filename.length != 41 || strcmp(entry->filename.data + 36, ".json") != 0 ||
            !tired_uuid_valid(entry->filename.data, 36))
            tired_error_set(&entry->error, TIRED_RECOVERY_REQUIRED, "service-inventory-name",
                            "Unrecognized entry in current service records.", 0);
        else
        {
            char uuid[37];
            memcpy(uuid, entry->filename.data, 36);
            uuid[36] = '\0';
            TiredServiceRecord record = {0};
            if (tired_service_record_read_budget(directory, layout, uuid, &budget, &record,
                                                 &entry->error))
            {
                entry->metadata = record.metadata;
                record.metadata = (TiredServiceMetadata){0};
            }
            tired_service_record_destroy(&record);
        }
        if (entry->error.status != TIRED_OK)
            inventory.complete = false;
    }
    for (size_t i = 0; i < inventory.count; ++i)
        for (size_t j = 0; j < i; ++j)
        {
            TiredServiceInventoryEntry *a = &inventory.entries[i], *b = &inventory.entries[j];
            if (a->metadata.unit_name.data != NULL && b->metadata.unit_name.data != NULL &&
                strcmp(a->metadata.unit_name.data, b->metadata.unit_name.data) == 0)
            {
                tired_error_set(&a->error, TIRED_CONFLICT, "service-inventory-duplicate",
                                "Multiple service records claim the same unit name.", 0);
                b->error = a->error;
                inventory.complete = false;
            }
        }
    if (!tired_directory_check(directory, error))
        goto done;
    if (fstat(fd, &after) != 0)
    {
        io_error(error);
        goto done;
    }
    if (before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
    {
        tired_error_set(error, TIRED_CONFLICT, "service-inventory-changed",
                        "Service inventory namespace changed during discovery.", 0);
        goto done;
    }
success:
    tired_service_inventory_destroy(output);
    *output = inventory;
    inventory = (TiredServiceInventory){0};
    tired_error_clear(error);
    ok = true;
done:
    if (scan != NULL)
        (void)closedir(scan);
    tired_directory_destroy(directory);
    tired_text_list_destroy(&names);
    tired_service_inventory_destroy(&inventory);
    return ok;
}
bool tired_service_inventory_find(const TiredServiceInventory *inventory, const char *unit,
                                  const TiredServiceMetadata **metadata, TiredError *error)
{
    assert(inventory != NULL && unit != NULL && metadata != NULL);
    if (!inventory->complete)
        return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "service-inventory-incomplete",
                               "Incomplete service inventory cannot establish unique ownership.",
                               0);
    for (size_t i = 0; i < inventory->count; ++i)
        if (strcmp(inventory->entries[i].metadata.unit_name.data, unit) == 0)
        {
            *metadata = &inventory->entries[i].metadata;
            tired_error_clear(error);
            return true;
        }
    return tired_error_set(error, TIRED_NOT_FOUND, "service-record-not-found",
                           "No service record names the selected unit.", 0);
}
