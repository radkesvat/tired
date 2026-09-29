# Publishing files

`TiredPublication` prepares a complete file in the destination directory under a
random UUID-based exclusive staging name. It writes at most 16 MiB, applies the
requested mode (`0600` for private data, `0644` for nonsecret unit bytes), syncs
the file, checks writer close, and syncs the staging directory entry. It retains
a descriptor that pins the staged inode. The destination directory handle must
outlive the publication handle.

Commit requires a held operation lock, validated destination directory and an
unchanged staged binding. The controller must associate the lock and destination
with the same selected scope and validate the bytes before calling commit.
The implementation uses `renameat2` with `RENAME_NOREPLACE`; a file, directory,
mask or dangling link at the destination prevents publication. There is no
check-then-overwriting-rename fallback. Unsupported filesystem behavior is an
error. Staging in the destination directory avoids cross-filesystem rename.

After rename, the API records `published=true` before syncing the directory.
Only a successful directory sync records `durable=true`. If that sync fails,
the controller must report published content with uncertain durability. A retry
checks the published file's binding, size and modification time and retries sync;
it does not rename again or treat the destination as an unrelated collision.
These booleans describe this operation, not a guarantee against later external
modification or loss on storage that does not honor sync semantics.

Preparation errors can return a handle with retained staging. Discard checks
the original inode binding, removes only unpublished staging, and syncs removal.
It preserves replacements and never removes a published destination. Destroy
only closes descriptors and frees memory; callers must surface discard failures
and the retained temporary name. A cancelled preparation and a published file
therefore have distinct cleanup paths.

`tired_publication_replace` handles an expected-existing destination. It verifies
the recorded before fingerprint and uses `RENAME_EXCHANGE` to atomically publish
the prepared file while retaining the displaced inode at the temporary name.
After syncing the directory it checks both bindings and the displaced fingerprint.
The controller must first persist intent and verified rollback material.

A foreign writer can race the before-check. If the displaced entry does not match,
the call fails with `published=true` and retains both entries for recovery; it does
not blindly exchange them back or delete either one. A retry verifies and syncs
without another exchange. The expected fingerprint cannot change after exchange,
and calling ordinary commit on an exchanged handle follows the same checks.
Discard and destroy preserve the displaced file. Its eventual cleanup requires
the transaction controller's ownership and retention checks.

After successful exchange, the temporary name contains the before-state, so an
artifact check against the after-state reports `different`. Recovery must interpret
that observation with the journal and destination state. Managed-file deletion
and complete rollback remain controller work. Metadata checks and advisory locks do not make the entire namespace immutable
against noncooperating writers.

Native tests cover complete-byte visibility, modes, existing-file and dangling-link
collisions, retry and discard semantics, replaced/modified staging, and injected
sync failures before publication and after rename. Failure injection verifies
state reporting and recovery branches; it is not power-loss qualification.
Replacement tests cover before-state mismatch, retained displaced bytes, sync
failure/retry, unchanged expectations and detection of displaced-file modification.

`tired_publication_reopen` uses the original staging UUID and journal-bound before/
after fingerprints under the scope lock. It accepts prepared staging, published
creation with no staging entry, or exchanged replacement with the before inode
still at the staging name. Missing, foreign or ambiguous combinations fail without
returning a handle. It pins and rechecks the after inode without renaming or deleting
anything. Reopened durability starts false; commit syncs the file and directory and
uses the same retry checks. A prepared replacement still requires the replacement
commit API with its expected before-state. The controller must establish journal
identity, approval and action ordering before reopening.
