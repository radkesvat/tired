# Scope mutation lock

Every cooperating writer uses the scope's `operation.lock`: system scope under
`/run/tired`, user scope under `$XDG_RUNTIME_DIR/tired`. The controller first
resolves and validates the runtime directory, then acquires `TiredOperationLock`.
The directory handle must remain alive until the lock is destroyed. The lock API
accepts a validated private directory, not a caller-selected lock filename.

Acquisition uses an exclusive nonblocking `flock`. A busy lock returns conflict
code `operation-in-progress` with a message that another operation is active.
There is no implicit wait or retry loop. This advisory lock coordinates tired
writers; it does not prevent administrators or other programs from changing files.

The directory must belong to the effective identity with mode `0700`. The lock
file must be empty, regular, owned by that identity, mode `0600`, and have exactly
one hard link. Existing entries are inspected without following symlinks and
are never truncated or repaired. A newly created file and its directory are
synced. Failure may leave an empty lock file, which a later writer can reuse.

Before mutation, check the lock again: directory trust, file permissions/type,
parent/name inode binding and the acquiring process identity must still hold.
Replacing the pathname invalidates the old holder's check. This check does not
eliminate races with noncooperating writers or replace root-path revalidation,
expected digests, approval checks and transaction recovery.

Release closes the descriptor without unlinking the file. Unlinking would allow
new writers to lock a different inode while another process still holds the old
one. Descriptors close on exec. A forked child must close its inherited copy
before unrelated work; it cannot treat the inherited handle as its own mutation
authority. Destruction closes rather than explicitly unlocking, so a child's
cleanup cannot unlock the parent's shared open-file description. The kernel
releases the lock when the last reference closes, including after process death.

Native tests cover contention within one process and between processes, fork
cleanup, death of a holder, reacquisition, replacement, unsafe permissions,
symlinks, hard links, FIFOs and preservation of unexpected nonempty files.
The transaction controller still needs to hold this lock across revalidation,
publication, manager operations, and durable commit or recovery bookkeeping.
