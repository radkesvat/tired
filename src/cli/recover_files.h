#ifndef TIRED_RECOVER_FILES_H
#define TIRED_RECOVER_FILES_H
#include "tired/file_artifact.h"
#include "tired/file_reconciliation.h"
#include "tired/transaction_inventory.h"
typedef struct
{
    TiredArtifactObservation staging, rollback, retained;
    TiredError staging_error, rollback_error, retained_error;
} TiredRecoveryArtifacts;
typedef struct
{
    TiredFileManifest manifest;
    TiredFileReconciliation observations;
    TiredRecoveryArtifacts *artifacts;
    bool artifacts_complete;
    TiredError error;
} TiredRecoveryFiles;
void tired_recover_files_collect(const TiredLayout *layout,
                                 const TiredTransactionInventoryEntry *entry, size_t *budget,
                                 TiredRecoveryFiles *files);
void tired_recover_files_destroy(TiredRecoveryFiles *files);
#endif
