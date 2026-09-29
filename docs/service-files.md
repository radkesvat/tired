# Inspecting managed files

`tired_service_files_inspect` compares a validated service record with its current
unit file and optional owned environment revision. Destinations come from the
trusted scope layout; recorded paths must agree. A scope or record-owner mismatch
fails without replacing the caller's previous result.

Each file has an independent result:

| State | Meaning |
| --- | --- |
| Unknown | Trust, read, type, size or stability checks prevented observation; a diagnostic is retained |
| Not required | The record has no owned environment revision |
| Missing | The destination or a required parent directory is absent |
| Drifted | An observed regular file disagrees with expected content, owner, mode or unit marker |
| Match | All applicable on-disk comparisons agree |

Inspection uses no-follow directory traversal and stable single-link regular-file
reads. Symlinks, including dangling links and masks, remain unknown with a type
conflict rather than being followed or treated as missing. Unit reads have a 4 MiB
bound; environment reads have a 16 MiB bound. No file contents are returned or
logged. The temporary unit snapshot is cleared before release.

For an existing file, separate flags report digest, owner and mode agreement.
The expected modes are 0644 for units and 0600 for owned environments. The unit
also requires the canonical first-line ownership comment with the record's UUID
and schema 1. Environment files have no ownership comment. Digests are compared
with the recorded installed bytes, never a newly rendered version of the model.
An unchanged replacement inode may match; touching timestamps is not drift.

A file match is descriptive disk evidence, not mutation authorization or proof
that systemd is running those bytes. Controllers must still validate the current
record, selected manager's fragment and drop-ins, pending transactions and approved
operation. They must revalidate under the scope lock before writing. This component
does not inspect external application configuration, adopt files, reload a manager,
repair drift or expose a CLI command.

Filesystem fixtures exercise missing and matching units, digest/mode/UUID drift,
hard links and dangling symlinks, absent/matching/changed environment revisions,
unsafe environment directories, partial results and atomic scope/owner failures.
