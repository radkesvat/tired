#include "tired/file_change.h"
#include "tired/file_retirement.h"
#include "tired/private_file.h"
#include "tired/publication.h"
#include <assert.h>
#include <errno.h>
#include <unistd.h>

bool tired_file_change_apply(const TiredLayout *layout, const TiredFileManifest *manifest,
                             size_t index, bool rollback, const TiredOperationLock *lock,
                             TiredFileChangeResult *result, TiredError *error)
{
    assert(layout != NULL && manifest != NULL && lock != NULL && result != NULL);
    *result = (TiredFileChangeResult){0};
    if (!tired_file_manifest_validate(manifest, error) || !tired_operation_lock_check(lock, error))
        return false;
    if (index >= manifest->count || layout->user_scope != manifest->prepared.user_scope)
        return tired_error_set(error, TIRED_INVALID, "file-change-input",
                               "File index or scope does not match the manifest.", 0);
    const TiredFileChange *change = &manifest->files[index];
    TiredResolvedFile resolved = {0};
    TiredDirectory *directory = NULL;
    TiredPublication *publication = NULL;
    TiredFileRetirement moved = {0};
    bool ok = false;
    if (!tired_file_target_resolve(layout, &change->target, &resolved, error) ||
        !tired_directory_open(resolved.directory.data, layout->user_scope ? geteuid() : 0,
                              resolved.private_directory, &directory, error))
        goto done;
    if (!change->after.exists)
    {
        ok = rollback ? tired_file_unretire(directory, resolved.name.data, change->rollback_uuid,
                                            &change->before, lock, &moved, error)
                      : tired_file_retire(directory, resolved.name.data, change->rollback_uuid,
                                          &change->before, lock, &moved, error);
        result->reached = moved.moved;
        result->durable = moved.durable;
    }
    else if (rollback && !change->before.exists)
    {
        TiredFileFingerprint actual = {0};
        if (!tired_file_fingerprint(directory, resolved.name.data, TIRED_PRIVATE_FILE_LIMIT,
                                    &actual, error))
            goto done;
        if (!actual.exists)
        {
            result->reached = true;
            if (fsync(tired_directory_fd(directory)) != 0)
            {
                tired_error_set(error, TIRED_RUNTIME_FAILED, "file-change-sync",
                                "Cannot sync the absent rollback destination.", errno);
                goto done;
            }
            if (!tired_file_fingerprint(directory, resolved.name.data, TIRED_PRIVATE_FILE_LIMIT,
                                        &actual, error))
                goto done;
            if (actual.exists)
            {
                tired_error_set(error, TIRED_CONFLICT, "file-change-recreated",
                                "Rollback destination appeared during absence verification.", 0);
                goto done;
            }
            result->durable = ok = true;
        }
        else
        {
            ok = tired_file_retire(directory, resolved.name.data, change->staging_uuid,
                                   &change->after, lock, &moved, error);
            result->reached = moved.moved;
            result->durable = moved.durable;
        }
    }
    else
    {
        if (rollback)
            ok = tired_publication_rollback(directory, resolved.name.data, change->staging_uuid,
                                            &change->before, &change->after, lock, &publication,
                                            error);
        else if (tired_publication_reopen(directory, resolved.name.data, change->staging_uuid,
                                          &change->before, &change->after, lock, &publication,
                                          error))
            ok = change->before.exists
                     ? tired_publication_replace(publication, lock, &change->before, error)
                     : tired_publication_commit(publication, lock, error);
        if (publication != NULL)
        {
            result->reached = tired_publication_published(publication);
            result->durable = tired_publication_durable(publication);
        }
    }
    if (ok)
        tired_error_clear(error);
done:
    /* Retained/staged entries belong to the transaction, including on failure. */
    tired_publication_destroy(publication);
    tired_directory_destroy(directory);
    tired_resolved_file_destroy(&resolved);
    return ok;
}
