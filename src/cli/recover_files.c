#include "recover_files.h"
#include "tired/manifest_storage.h"
#include <string.h>
#include <unistd.h>

void tired_recover_files_destroy(TiredRecoveryFiles *files)
{
    tired_file_manifest_destroy(&files->manifest);
    tired_file_reconciliation_destroy(&files->observations);
    *files = (TiredRecoveryFiles){0};
}
void tired_recover_files_collect(const TiredLayout *layout,
                                 const TiredTransactionInventoryEntry *entry, size_t *budget,
                                 TiredRecoveryFiles *files)
{
    tired_recover_files_destroy(files);
    TiredDirectory *root = NULL, *transaction = NULL;
    if (!tired_directory_open(layout->paths[TIRED_PATH_TRANSACTIONS].data,
                              layout->user_scope ? geteuid() : 0, true, &root, &files->error) ||
        !tired_directory_child(root, entry->directory_name.data, false, true, &transaction,
                               &files->error) ||
        !tired_manifest_load(transaction, &files->manifest, &files->error))
        goto done;
    const TiredTransactionRecord *prepared = &files->manifest.prepared;
    if (prepared->user_scope != layout->user_scope ||
        strcmp(prepared->transaction_uuid, entry->directory_name.data) != 0 ||
        strcmp(prepared->unit_name.data, entry->unit_name.data) != 0)
    {
        tired_error_set(&files->error, TIRED_CONFLICT, "recovery-files-identity",
                        "Transaction file identity changed during inspection.", 0);
        goto done;
    }
    (void)tired_file_reconcile_budget(layout, &files->manifest, budget, &files->observations,
                                      &files->error);
done:
    tired_directory_destroy(transaction);
    tired_directory_destroy(root);
}
