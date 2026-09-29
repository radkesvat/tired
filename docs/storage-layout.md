# Storage locations

The layout resolver derives mutable-state paths independently of the executable's
installation prefix. System scope always uses these locations:

| Data | Location |
| --- | --- |
| Configuration | `/etc/tired/config.json` |
| Administrator profiles | `/etc/tired/profiles.d` |
| Revision environment service directories | `/etc/tired/services` |
| Unit files | `/etc/systemd/system` |
| Current service records | `/var/lib/tired/services` |
| Prior control-plane revisions | `/var/lib/tired/history` |
| Recovery transactions | `/var/lib/tired/transactions` |
| Mutation lock | `/run/tired/operation.lock` |

User scope puts configuration, profiles and environment service directories under
`$XDG_CONFIG_HOME/tired`, units under `$XDG_CONFIG_HOME/systemd/user`, service
records/history/transactions under `$XDG_STATE_HOME/tired`, and the lock at
`$XDG_RUNTIME_DIR/tired/operation.lock`. Unset or empty config/state variables
default to the account database home plus `.config` and `.local/state`. `HOME`
does not override the account database. Runtime has no guessed fallback.

System discovery never reads user environment paths. User discovery requires
matching real/effective user and group IDs. The pure resolver can also receive
explicit user path inputs for controlled callers and tests; these inputs cannot
override system destinations.

Paths must be absolute, have no dot/traversal components or repeated interior
separators, and contain valid text without control characters. Trailing separators
are removed. Components are bounded to 255 bytes and complete paths to 4096 bytes,
including appended suffixes. Failed resolution preserves the prior layout.

Before creating a user unit, the controller must check that the resolved unit
directory occurs in the selected manager's UnitPath. Live name discovery enforces
this check using the scope retained by manager identity discovery. The check compares lexical
paths with trailing separators ignored; it refuses mismatches rather than guessing
that a service will be visible. It does not resolve filesystem aliases.

These APIs only derive and compare paths. They create nothing and do not prove
ownership, accessibility or authority to write. Storage operations must separately
validate directory ownership and permissions, reject unsafe links, use private
state directories and files, and perform mutations through validated descriptors.
The transaction store, record schema and lock operations are separate components.

## Directory access

`TiredDirectory` opens existing paths one component at a time using directory
descriptors and refuses symlinks. Every ancestor must belong to root or the
selected storage owner and forbid group/other writes. Shared writable ancestors,
including sticky temporary directories, are unsuitable for these persistent-state
operations. Private final directories must have the selected owner and exactly
mode `0700`; ordinary trusted ancestors may be readable by others.

Opening never creates anything. A separate child operation accepts a single
validated name and an explicit creation flag. Creation runs as the selected owner,
requests `0700`, validates the resulting directory, and syncs it and its parent.
Existing directories are checked without changing their permissions or owner.
A restrictive process umask or inherited special permission bits can make creation
fail validation; the API does not silently repair permissions. A failed creation
or sync can leave an empty directory. Cleanup must inspect that entry before
removing it, rather than recursively deleting a pathname.

Handles own close-on-exec descriptors for the directory and its immediate parent.
Rechecking validates permissions and confirms the parent/name still denotes the
same directory inode. A child remains usable after its parent's handle is released.
Renaming or replacing that child causes a later check to fail. Descriptor pinning
does not freeze the entire ancestor namespace; transaction controllers must
reopen/revalidate scope roots before committing and hold their mutation lock.
Callers use the borrowed descriptor for subsequent descriptor-relative operations.

Tests cover private creation, existing permission refusal, replaced bindings,
descriptor lifetime, changed ownership, writable parents, symlinks at final and
intermediate components, dangling links, FIFOs, traversal and missing paths.

## Private files

Private-file operations accept one validated component and an existing directory
handle. The directory must be owned by the effective identity with mode `0700`.
Files must be regular, owned by that identity, mode `0600`, and have exactly one
link. Reads reject symlinks, directories and special files before opening, then
check the opened descriptor and its directory entry. The directory and binding
are checked again after reading; the bounded reader also checks file metadata
for changes. Failed reads preserve the caller's previous output.

Creation uses exclusive descriptor-relative open, writes every byte, syncs the
file, rechecks its binding, checks close, then syncs the parent directory. Existing
entries are never overwritten or truncated. These operations do not acquire the
scope lock themselves; the transaction controller holds it across preparation and
publication. Private files may contain binary bytes, so callers use the returned
length and separately validate JSON or other application formats. Each read chooses
its limit; the maximum private-file size is 16 MiB. Individual formats retain their
own lower bounds where required.

Exclusive creation is suitable for private staging and immutable revision entries.
The name becomes visible before the write finishes, so this is not atomic
publication of a current record. A write, sync, close or validation failure can
leave a partial file; recovery must inspect it rather than blindly deleting the
pathname. A later create refuses that entry. Transaction records must distinguish
prepared content from published or committed content, and current-record updates
require a separate atomic publication operation.

Native coverage includes binary/empty reads, limits and preserved output, existing
entry refusal, permissions, owner changes, symlinks, hard links, FIFOs, directories,
and a child process file-size limit that forces a partial write and verifies that
the incomplete file remains without being overwritten.

## File fingerprints

Recovery and mutation revalidation can observe a single file through a validated
directory descriptor. A fingerprint contains known absence, or the regular file's
device/inode identity, owner/group, permission bits, size and SHA-256 digest.
It retains no file contents. Fingerprints themselves belong in private control-plane
state; a digest is neither encryption nor proof of authenticity against root.

Inspection refuses symlinks, nonregular files and multiple hard links. It streams
at most the caller's byte limit, capped at 16 MiB, checks metadata before/after
reading, and checks that the directory entry still denotes the open inode. Detected
replacement, growth or other changes fail without replacing the previous result.
Only ENOENT establishes absence; errors never become an absent-file observation.

Equality compares absence or identity, owner/group, mode, size and digest. File
timestamps are used to detect changes during reading, not for equality: touching
unchanged content is not drift, but replacing an inode with identical bytes is.
Observed ownership and permissions are descriptive, not write authorization.
Controllers must still verify managed ownership, expected manifests and scope,
hold the mutation lock, and publish with the required filesystem safeguards.
The observations do not make a concurrently writable file immutable.

Native tests cover known SHA-256 vectors, absent/empty files, byte limits, mode
changes, same-content replacement, symlinks/hard links/FIFOs and deterministic
replacement/growth during reading. Manifest storage and recovery comparisons
remain subsequent integration work.

Fingerprints have a strict schema-1 JSON representation for private persistence.
Absence is exactly `schema_version` plus `exists: false`; it carries no invented
device, owner, size or digest. Existing-file objects require all identity/metadata
fields and a canonical 64-character lowercase SHA-256 value. Parsing rejects
unknown fields, duplicate keys, missing metadata, floating-point/negative/overflowing
integer values, permission bits outside `07777`, and sizes above the inspection
limit. Native device/inode/owner/group ranges are enforced before conversion.

Serialization preserves unsigned integer values exactly, including values above
the signed 64-bit range. Consumers must not pass identities through floating point.
Failures preserve previous outputs. The installed `file-fingerprint.schema.json`
documents the structural format; native parsing additionally enforces strict integer
syntax/types. Tests cover width boundaries, absence versus invalid metadata, and
an actual observed fingerprint persisted through private-file storage and read back.
These values remain evidence to compare, not permission to modify a file.
