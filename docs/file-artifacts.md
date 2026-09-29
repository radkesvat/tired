# Inspecting recovery artifacts

`tired_file_artifact_observe` reads one manifest artifact without modifying it.
The caller selects a validated file change, trusted scope layout and transaction
directory, and supplies a byte limit of at most 16 MiB. Repeated callers must also
enforce an aggregate work budget.

Staged files live at `.tired-<staging_uuid>.tmp` in the typed destination directory.
They must match the after fingerprint's inode, device, ownership, mode, size and
digest. A same-content replacement with a different inode is a mismatch.

Rollback copies live at `artifacts/<rollback_uuid>` under the private transaction
directory. They must be regular single-link files owned by the effective user with
mode 0600, and match the before fingerprint's size and digest. Their own inode,
group and mode need not reproduce the original destination metadata. Restoring
that metadata is a separate controller responsibility.

Successful observations report `MATCH`, `MISSING` or `DIFFERENT`. Unreadable,
unsafe, changing or oversized entries fail and preserve the previous output;
callers must use the return value and retain the error rather than reusing stale
observations. Missing trusted parent directories establish a missing artifact.
Symlinks and permission errors never establish absence.

Missing staging can be expected after publication, so these observations alone
cannot decide whether recovery should finish or roll back. They must be combined
with destination comparisons, journal progress and manager observations. This API
does not prove approval, create backups, authorize writes or perform recovery.

Native tests cover missing parents/files, private rollback copies with distinct
inodes, changed modes/digests, exact staged identity, size limits, symlinks and
preservation on failure.
