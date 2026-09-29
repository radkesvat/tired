#ifndef TIRED_OPERATION_LOCK_H
#define TIRED_OPERATION_LOCK_H
#include "tired/directory.h"
typedef struct TiredOperationLock TiredOperationLock;
/* Acquire operation.lock in an already validated private runtime directory owned
 * by the effective UID. The directory must outlive the lock. Immediate conflict
 * on contention; never sleeps, truncates, repairs or removes an existing entry.
 * A new empty 0600 lock file may remain after failure or release. */
bool tired_operation_lock_acquire(TiredDirectory *runtime, TiredOperationLock **lock,
                                  TiredError *error);
/* Check directory, file binding, permissions and acquiring process identity before
 * mutation. Does not replace scope-root revalidation or transaction recovery. */
bool tired_operation_lock_check(const TiredOperationLock *lock, TiredError *error);
/* Close to release. Never unlinks or explicitly unlocks a shared fork descriptor.
 * Forked children must close their inherited copy before unrelated work; exec
 * closes it automatically. They cannot use check() as mutation authorization. */
void tired_operation_lock_destroy(TiredOperationLock *lock);
#endif
