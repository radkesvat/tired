# Retaining files during removal

`tired_file_retire` removes a managed destination from its active name by moving
it to `.tired-<uuid>.removed` in the same directory with `RENAME_NOREPLACE`.
The retained inode remains available for recovery. Nothing is unlinked or
overwritten. The caller must persist the UUID and removal intent, verify managed
ownership, and retain rollback material before invoking this primitive.

Under the supplied scope lock, the operation verifies the expected fingerprint and
an absent retained name, performs the move, and syncs the directory. It then checks
that the destination is absent and the retained file still matches. A retry accepts
that same absent-destination/matching-retained-file state and syncs without moving
again. Both absent, an existing recovery name before removal, or a newly created
foreign destination produce a conflict.

The result resets on every invocation. `moved` becomes true once this call moves
the entry or recognizes the matching retained inode with an absent destination.
It remains true if a later sync/check fails. False does not prove that an earlier
invocation never moved a file. `durable` requires successful directory sync and
post-move checks; these observations can become stale immediately.

Advisory locking cannot exclude a foreign writer between the fingerprint check
and rename. A raced entry is retained rather than deleted, and a post-move mismatch
requires recovery inspection. The API does not blindly move it back. Terminal
transaction cleanup and restoring a removed entry remain separate controller work.

Native tests cover fingerprint mismatch, retained-name collisions, sync failure and
retry, retained content, and preservation of a recreated foreign destination.
