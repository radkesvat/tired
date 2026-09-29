#ifndef TIRED_RECOVER_FILES_H
#define TIRED_RECOVER_FILES_H
#include "tired/file_reconciliation.h"
#include "tired/transaction_inventory.h"
typedef struct
{
    TiredFileManifest manifest;
    TiredFileReconciliation observations;
    TiredError error;
} TiredRecoveryFiles;
void tired_recover_files_collect(const TiredLayout *layout,
                                 const TiredTransactionInventoryEntry *entry, size_t *budget,
                                 TiredRecoveryFiles *files);
void tired_recover_files_destroy(TiredRecoveryFiles *files);
#endif
