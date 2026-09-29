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
Manager UnitPath discovery, transaction inventory loading, automatic-name selection
and commit-time reservation are not yet connected to this checker.
