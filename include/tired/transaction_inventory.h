#ifndef TIRED_TRANSACTION_INVENTORY_H
#define TIRED_TRANSACTION_INVENTORY_H
#include "tired/transaction_journal.h"
typedef struct
{
    TiredText directory_name, unit_name;
    TiredTransactionProgress progress;
    TiredError error;
} TiredTransactionInventoryEntry;
typedef struct
{
    TiredTransactionInventoryEntry *entries;
    size_t count;
    bool complete;
    TiredTextList pending_names;
} TiredTransactionInventory;
/* Read <transaction-uuid>/journal under a private transactions root. Up to 1024
 * entries, sorted by raw name. Invalid entries retain diagnostics without hiding
 * valid neighbors. Raw directory names require escaping before display.
 * Empty/mismatched/corrupt journals, staging and unresolved rename manifests make
 * the inventory incomplete. No creation, deletion or mutation. Atomic output. */
bool tired_transaction_inventory_read(TiredDirectory *root, bool user_scope,
                                      TiredTransactionInventory *inventory, TiredError *error);
/* Only a complete inventory may supply reservations to collision discovery. */
bool tired_transaction_inventory_pending(const TiredTransactionInventory *inventory,
                                         const TiredTextList **names, TiredError *error);
void tired_transaction_inventory_destroy(TiredTransactionInventory *inventory);
#endif
