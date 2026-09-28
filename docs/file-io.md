# Frontend file I/O

User-selected inputs are opened without blocking on a FIFO, then checked to be
regular files. Input symlinks may resolve to regular files; this is selection by the
unprivileged frontend, not proof of administrative trust. Byte limits apply before
and during reading. Observed size or timestamp changes cause a conflict. This check
is not a lock or a guarantee against an attacker who can rewrite the source.

Reads handle interruptions and publish a new owned buffer only after successful
completion and descriptor closure. Binary bytes, including NUL, are preserved by
the I/O layer; text parsers enforce their own grammar afterward. Empty files are
valid reads when the caller's format permits them.

Explicit private exports create a new file with mode 0600, exclusive creation, and
no final-component symlink following. Existing files and symlinks are conflicts.
Writes handle partial progress and interruptions, then fsync and close the file.
On failure a partial private file may remain at the requested path. The function
does not unlink a pathname that another process could have replaced. These helpers
are not the privileged transactional storage layer and do not authorize arbitrary
helper destinations.

Relative paths are joined to the captured absolute directory without simplifying
symlink-sensitive `..`. Service identifiers use kernel randomness and UUIDv4 version
and variant bits. Random-source failure propagates rather than falling back to time,
process IDs, counters, or weak random generators.
