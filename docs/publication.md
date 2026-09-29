# Publishing new files

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

This component handles expected-absent destinations. Replacement of existing
managed files, expected-content digests, durable manifests and rollback remain
transaction-controller responsibilities. Metadata checks and advisory locks do
not make the entire namespace immutable against noncooperating writers.

Native tests cover complete-byte visibility, modes, existing-file and dangling-link
collisions, retry and discard semantics, replaced/modified staging, and injected
sync failures before publication and after rename. Failure injection verifies
state reporting and recovery branches; it is not power-loss qualification.
