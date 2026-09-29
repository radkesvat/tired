#define _GNU_SOURCE
#include "tired/file_retirement.h"
#include "tired/io.h"
#include "tired/private_file.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool conflict(TiredError *error)
{
    return tired_error_set(error, TIRED_CONFLICT, "retirement-state",
                           "Destination or retained file differs from the expected removal state.",
                           0);
}
static bool move(TiredDirectory *directory, const char *name, const char *uuid,
                 const TiredFileFingerprint *expected, const TiredOperationLock *lock, bool restore,
                 TiredFileRetirement *result, TiredError *error)
{
    assert(directory != NULL && name != NULL && uuid != NULL && expected != NULL && lock != NULL &&
           result != NULL);
    *result = (TiredFileRetirement){0};
    if (!expected->exists || !tired_uuid_valid(uuid, strnlen(uuid, 37)))
        return tired_error_set(error, TIRED_INVALID, "retirement-input",
                               "File move requires an existing before-state and canonical UUID.",
                               0);
    TiredText encoded = {0};
    bool valid = tired_file_fingerprint_encode(expected, &encoded, error);
    tired_text_destroy(&encoded);
    if (!valid || !tired_operation_lock_check(lock, error))
        return false;
    char retained[52];
    (void)snprintf(retained, sizeof(retained), ".tired-%s.removed", uuid);
    if (strcmp(name, retained) == 0)
        return conflict(error);
    const char *source = restore ? retained : name;
    const char *target = restore ? name : retained;
    TiredFileFingerprint destination = {0}, saved = {0};
    if (!tired_file_fingerprint(directory, source, TIRED_PRIVATE_FILE_LIMIT, &destination, error) ||
        !tired_file_fingerprint(directory, target, TIRED_PRIVATE_FILE_LIMIT, &saved, error))
        return false;
    int fd = tired_directory_fd(directory);
    if (destination.exists)
    {
        if (saved.exists || !tired_file_fingerprint_equal(&destination, expected))
            return conflict(error);
        if (renameat2(fd, source, fd, target, RENAME_NOREPLACE) != 0)
            return tired_error_set(error, errno == EEXIST ? TIRED_CONFLICT : TIRED_RUNTIME_FAILED,
                                   "retirement-rename",
                                   "Cannot move file between active and retained names.", errno);
        result->moved = true;
    }
    else
    {
        if (!tired_file_fingerprint_equal(&saved, expected))
            return conflict(error);
        result->moved = true;
    }
    if (fsync(fd) != 0)
        return tired_error_set(error, TIRED_RUNTIME_FAILED, "retirement-sync",
                               "File moved but directory durability is unknown.", errno);
    if (!tired_file_fingerprint(directory, target, TIRED_PRIVATE_FILE_LIMIT, &saved, error) ||
        !tired_file_fingerprint(directory, source, TIRED_PRIVATE_FILE_LIMIT, &destination, error))
        return false;
    if (destination.exists || !tired_file_fingerprint_equal(&saved, expected))
        return conflict(error);
    result->durable = true;
    tired_error_clear(error);
    return true;
}
bool tired_file_retire(TiredDirectory *directory, const char *name, const char *uuid,
                       const TiredFileFingerprint *expected, const TiredOperationLock *lock,
                       TiredFileRetirement *result, TiredError *error)
{
    return move(directory, name, uuid, expected, lock, false, result, error);
}
bool tired_file_unretire(TiredDirectory *directory, const char *name, const char *uuid,
                         const TiredFileFingerprint *expected, const TiredOperationLock *lock,
                         TiredFileRetirement *result, TiredError *error)
{
    return move(directory, name, uuid, expected, lock, true, result, error);
}
