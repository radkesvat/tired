#include "tired/file_reconciliation.h"
#include "tired/private_file.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

void tired_file_reconciliation_destroy(TiredFileReconciliation *result)
{
    if (result == NULL)
        return;
    free(result->files);
    *result = (TiredFileReconciliation){0};
}

bool tired_file_reconcile(const TiredLayout *layout, const TiredFileManifest *manifest,
                          TiredFileReconciliation *output, TiredError *error)
{
    assert(layout != NULL && manifest != NULL && output != NULL);
    if (!tired_file_manifest_validate(manifest, error))
        return false;
    if (layout->user_scope != manifest->prepared.user_scope)
        return tired_error_set(error, TIRED_CONFLICT, "reconcile-scope",
                               "Manifest scope differs from the selected layout.", 0);
    TiredFileReconciliation result = {.count = manifest->count, .complete = true};
    if (result.count != 0)
    {
        result.files = calloc(result.count, sizeof(*result.files));
        if (result.files == NULL)
            return tired_error_set(error, TIRED_INTERNAL, "allocation",
                                   "Cannot allocate file observations.", 0);
    }
    size_t remaining = 64U * 1024U * 1024U;
    for (size_t i = 0; i < result.count; ++i)
    {
        const TiredFileChange *change = &manifest->files[i];
        TiredFileObservation *observed = &result.files[i];
        TiredResolvedFile resolved = {0};
        TiredDirectory *directory = NULL;
        bool known = false;
        if (!tired_file_target_resolve(layout, &change->target, &resolved, &observed->error))
            goto next;
        if (!tired_directory_open(resolved.directory.data, layout->user_scope ? geteuid() : 0,
                                  resolved.private_directory, &directory, &observed->error))
        {
            known = observed->error.status == TIRED_NOT_FOUND;
            goto next;
        }
        if (remaining == 0)
        {
            tired_error_set(&observed->error, TIRED_RECOVERY_REQUIRED, "reconcile-budget",
                            "File inspection byte budget exhausted.", 0);
            goto next;
        }
        size_t limit = remaining < TIRED_PRIVATE_FILE_LIMIT ? remaining : TIRED_PRIVATE_FILE_LIMIT;
        known = tired_file_fingerprint(directory, resolved.name.data, limit, &observed->actual,
                                       &observed->error);
        remaining -= known ? (size_t)observed->actual.size : limit;
    next:
        if (known)
        {
            tired_error_clear(&observed->error);
            observed->state = tired_file_fingerprint_equal(&observed->actual, &change->before)
                                  ? TIRED_FILE_BEFORE
                              : tired_file_fingerprint_equal(&observed->actual, &change->after)
                                  ? TIRED_FILE_AFTER
                                  : TIRED_FILE_FOREIGN;
            result.foreign |= observed->state == TIRED_FILE_FOREIGN;
        }
        else
            result.complete = false;
        tired_directory_destroy(directory);
        tired_resolved_file_destroy(&resolved);
    }
    tired_file_reconciliation_destroy(output);
    *output = result;
    tired_error_clear(error);
    return true;
}
