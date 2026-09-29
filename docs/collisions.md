# Service-name collision observations

The collision checker combines three supplied sources: a successful manager query
for the exact service name, the validated pending-transaction name inventory, and
all relevant unit-load/destination directories. A missing or failed manager query
cannot become a negative collision result. The caller must supply complete scope
information; the checker does not discover paths or read transaction records itself.

Manager file-state availability, manager-object existence and a pending reservation
each reserve the name. Otherwise the checker inspects the exact `.service` entry
and its `.service.d` path in directory order. Any existing entry reserves the name,
including vendor files, aliases, `/dev/null` masks, dangling symlinks, nonregular
files and empty drop-in directories. It never opens the unit entry or follows its
symlink target. No contents, executable or workload are read or run.

Directory symlinks are followed because collision observation must match the actual
manager search path, including distribution directory aliases. This grants no
authority to write through that path. Mutation code must separately validate its
destination and ownership. Missing load locations are empty; other access/open/stat
errors leave availability unknown and return failure.

At most 256 absolute directories are accepted, each bounded to 4096 bytes and
1 MiB combined. At least one directory is required. Pending names are bounded to
4096 and use the same managed service-name grammar. Results report the first
collision source and, for filesystem findings, the observed path. Updates are
atomic on failure.

`NONE` is only a tentative observation. It is neither a namespace reservation nor
permission to overwrite anything. The future naming controller must reject explicit
collisions, try deterministic suffixes for automatic names, and repeat checks under
the mutation lock before committing with non-overwriting filesystem operations.
Generic and hierarchical drop-in effects remain a separate effective-configuration
validation concern.

Tests cover manager and pending reservations, ordinary files, masks, dangling links,
FIFOs, unit-shaped directories, drop-in paths, directory aliases, missing locations,
invalid load locations, mismatched/incomplete manager results and failure atomicity.
The native load-path query obtains the manager's UnitPath string array through the
verified unique bus name. It requires a nonempty list, applies the same path/count
bounds and preserves order, duplicates and directory-alias spelling. Missing or
malformed UnitPath is an error, not an invitation to guess a smaller search path.
Queries use one monotonic deadline, support cancellation/poll integration, and hide
results after manager identity invalidation. The identity must outlive the query.

Native tests cover typed variants, relative/control/oversized paths, empty and
excessive arrays, failure preservation, destination/authorization flags, cancellation,
timeout and invalidation. Live manager integration retrieves the host's UnitPath.

## Candidate selection

`TiredNameSelection` copies the complete location and pending-name inventories
and proposes the unsuffixed name first. Feed it a successful manager query for
that exact name. It combines the observation with filesystem inspection, then
either reports tentative availability or advances automatic names to `-2`,
`-3`, and so on. Explicit names return a conflict without changing the candidate.
Unknown manager or filesystem results do not advance or approve a name.

The controller schedules each asynchronous manager query and imposes a deadline
and cancellation; selection itself has no arbitrary suffix search cutoff. The
candidate accessor returns a full unit name, whereas the unit-query API accepts
a normalized base without the final `.service` suffix. A successful selection
does not reserve the name. Commit-time locking, fresh inventories, rechecking,
and approval invalidation remain the mutation controller's responsibility.

`TiredNameQuery` coordinates live discovery: it obtains UnitPath through the
authenticated manager identity, includes the supplied system destination if not
already listed, and queries each candidate before asking the selector to inspect
it. For user scope, it rejects a destination absent from UnitPath before querying
any candidate. Scope comes from identity discovery, including for a root user manager.
The caller supplies the complete pending-transaction inventory for the selected
scope. The combined location list retains the 256-directory/1 MiB limit; exceeding
it fails rather than omitting paths. All inputs are copied. The identity must
outlive the coordinator, and later owner invalidation hides a completed result.

One monotonic deadline covers load-path discovery and every candidate, with at
most one candidate inspection per step. Poll integration includes that deadline;
cancellation detaches active queries. Filesystem operations are synchronous and
can delay a step on a stalled filesystem; a result after the deadline is rejected.
This API does not perform commit-time locking or read the transaction store.

Broker fixtures exercise deterministic suffixes across manager, pending and
filesystem collisions, rejection of a user destination outside UnitPath, explicit-name conflicts,
malformed paths, cancellation, a deadline spanning multiple candidates and owner
invalidation. Live integration checks rejection of an existing explicit name
without modifying the manager or its files.

`tired_name_query_discover` is the production admission path. It derives the
selected identity's scope layout, loads the transaction inventory, requires a
complete reservation list, and starts the asynchronous name query with the
resolved unit destination. System destinations cannot be redirected by caller
environment variables. User destinations still must appear in manager UnitPath.
All copied inventory/layout data can be released once the query starts.

Filesystem inventory discovery is synchronous; the supplied timeout covers the
subsequent asynchronous manager/name search. The low-level start API remains for
callers already holding validated complete scope information and for fixtures.
The higher-level controller still must perform authorized access to private
system state and repeat discovery under the mutation lock before publication.
Native fixtures exercise XDG-derived user discovery with missing transaction
storage, deterministic suffix selection and rejection of invalid paths before
queuing candidate queries.
