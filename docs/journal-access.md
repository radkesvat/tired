# Journal access observations

An empty native journal iterator is insufficient evidence that a service has no
logs. The baseline library can silently skip unreadable files while opening its
default search paths, and can remove damaged files during iteration.

The access inventory separately enumerates `/run/log/journal` and
`/var/log/journal`. It follows the default namespace and local-file policy: runtime
storage admits machine-ID subdirectories; persistent storage admits the local
machine-ID directory. Files directly in each root are also considered. User-scope
inventory includes system journals and journals named for the selected UID. System
scope includes all file types because service messages can require user journals.
Native iteration uses exact unit/UID [selection](journal-selection.md).

Each candidate is opened read-only without following its final symlink, checked
as a regular file, and opened individually by the native journal library. The
native filename API uses `/proc/self/fd/N` for the pinned descriptor. This avoids
re-resolving the original pathname and avoids the descriptor-opener assertion
observed in the systemd 249 baseline. The caller needs a functioning procfs for
this check. There is no privilege elevation, content iteration, or host mutation.

Reports contain examined-entry, directory, candidate-file, opened-file,
missing-root, and issue counts plus the first issue. Missing roots are distinct
from permission failures. Format errors, unexpected object types, symlinks,
disappearing files, and enumeration failures produce an incomplete report.
Enumeration stops at 4096 directory entries across both roots; hitting the limit
before exhausting the inventory is also incomplete. Paths and file content are
not included in diagnostics.

`complete` describes only this bounded enumeration and file-opening observation.
It is not a content-integrity result, a stable snapshot, or a guarantee that the
native iterator subsequently reports every internal read error. Callers must
surface incomplete access, distinguish absent storage, and recheck after file-set
invalidation. Concurrent rotation can invalidate any prior observation. A command
must not silently turn these limitations into a confident “no logs” result.

Private-directory tests cover missing and empty storage, user-file isolation,
malformed journal headers, symlinks, FIFOs, local/runtime machine filtering,
permission denial, and scan limits. The production logs command also has native tests; populated host journal and
rotation evidence are recorded in release qualification.
