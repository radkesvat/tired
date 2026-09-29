#ifndef TIRED_FILE_BACKUP_H
#define TIRED_FILE_BACKUP_H
#include "tired/file_fingerprint.h"
#include "tired/publication.h"
/* Create one UUID-named private rollback copy in an already opened artifacts
 * directory. Source must match an existing recorded fingerprint before and after
 * staging. Publish under the supplied scope lock with no replacement, then sync.
 * Both directories outlive the returned publication handle. Failure may return
 * a handle for retry/discard; never blindly unlink. Does not authorize rollback
 * or make a changing source stable after return. Caller selects trusted paths. */
bool tired_file_backup(TiredDirectory *source, const char *name,
                       const TiredFileFingerprint *expected, TiredDirectory *artifacts,
                       const char *uuid, const TiredOperationLock *lock,
                       TiredPublication **publication, TiredError *error);
#endif
