#include "recover_files.h"
#include "tired/manifest_storage.h"
#include "tired/private_file.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void tired_recover_files_destroy(TiredRecoveryFiles *files)
{
    tired_file_manifest_destroy(&files->manifest);
    tired_file_reconciliation_destroy(&files->observations);
    free(files->artifacts);
    *files = (TiredRecoveryFiles){0};
}
static bool artifact(const TiredLayout *layout, TiredDirectory *transaction,
                     const TiredFileChange *change, bool rollback, size_t *budget,
                     TiredArtifactObservation *output, TiredError *error)
{
    if (!(rollback ? change->before.exists : change->after.exists))
        return true;
    if (*budget == 0)
        return tired_error_set(error, TIRED_RECOVERY_REQUIRED, "recovery-artifact-budget",
                               "Recovery artifact byte budget exhausted.", 0);
    size_t limit = *budget < TIRED_PRIVATE_FILE_LIMIT ? *budget : TIRED_PRIVATE_FILE_LIMIT;
    bool ok =
        tired_file_artifact_observe(layout, transaction, change, rollback, limit, output, error);
    *budget -= ok ? (size_t)output->actual.size : limit;
    return ok;
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
    if (!tired_file_reconcile_budget(layout, &files->manifest, budget, &files->observations,
                                     &files->error))
        goto done;
    files->artifacts_complete = true;
    if (files->manifest.count != 0)
    {
        files->artifacts = calloc(files->manifest.count, sizeof(*files->artifacts));
        if (files->artifacts == NULL)
        {
            tired_error_set(&files->error, TIRED_INTERNAL, "allocation",
                            "Cannot allocate recovery artifact observations.", 0);
            goto done;
        }
    }
    for (size_t i = 0; i < files->manifest.count; ++i)
    {
        TiredRecoveryArtifacts *observed = &files->artifacts[i];
        if (!artifact(layout, transaction, &files->manifest.files[i], false, budget,
                      &observed->staging, &observed->staging_error))
            files->artifacts_complete = false;
        if (!artifact(layout, transaction, &files->manifest.files[i], true, budget,
                      &observed->rollback, &observed->rollback_error))
            files->artifacts_complete = false;
    }
done:
    tired_directory_destroy(transaction);
    tired_directory_destroy(root);
}
