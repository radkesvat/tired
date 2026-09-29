#ifndef TIRED_SERVICE_INVENTORY_H
#define TIRED_SERVICE_INVENTORY_H
#include "tired/service_record_storage.h"
typedef struct
{
    TiredText filename; /* Raw filename bytes: escape before display. */
    TiredServiceMetadata metadata;
    TiredError error;
} TiredServiceInventoryEntry;
typedef struct
{
    TiredServiceInventoryEntry *entries;
    size_t count;
    bool complete;
} TiredServiceInventory;
/* Read-only bounded inventory of current records. <=1024 entries, 64 MiB shared
 * read budget. Invalid neighbors retain diagnostics; duplicate unit names mark
 * both entries conflicting. Retains metadata only, not private argv/environment.
 * Missing root yields complete empty inventory. Other root failures propagate.
 * Atomic output; no creation, repair, authoritative index or mutation. */
bool tired_service_inventory_load(const TiredLayout *layout, TiredServiceInventory *output,
                                  TiredError *error);
/* Resolve a full unit name only from complete unambiguous inventory. Borrowed
 * result; caller reloads and verifies current record/files before using it. */
bool tired_service_inventory_find(const TiredServiceInventory *inventory, const char *unit,
                                  const TiredServiceMetadata **metadata, TiredError *error);
void tired_service_inventory_destroy(TiredServiceInventory *inventory);
#endif
