# Reading manager-reported configuration files

`tired_observed_file_read` captures a file named by a verified manager observation
for display. It does not resolve arbitrary helper-supplied destinations or authorize
mutation. The caller must associate the reported path with the selected unit and
manager scope before calling it.

Directory aliases are resolved component by component, with a maximum of 32 links.
All traversed directories must belong to root or the selected scope owner and must
not be group/other writable. Links must belong to root or that owner; their binding
and metadata are checked around `readlinkat`. Link targets receive the same traversal
checks. Relative targets and `..` are interpreted after earlier aliases have been
resolved, rather than lexically collapsing away potentially untrusted directories.
This permits normal merged-`/usr` directory aliases without permitting traversal
through a writable directory that happens to point back into trusted storage.
Procfs aliases are rejected: magic-link text is not ordinary pathname substitution
and can refer into another process's namespace.

The final file is read through the existing stable fingerprint/snapshot API. It
must be a regular file with one link; final symlinks are never followed. File owner
and mode remain descriptive evidence, so readable foreign edits can be inspected.
The result retains an alias-expanded path, a SHA-256 fingerprint and exact bytes.
Missing final entries are known absence. Missing parents, access failures, unsafe
paths and observed changes retain errors; prior output survives failure.

Each file is bounded to 4 MiB and shares a caller's remaining byte budget. Successful
reads charge actual bytes; failed reads charge their bounded allowance. Returned
bytes can contain secrets or terminal controls and require redaction/escaping before
display. Destruction clears retained bytes before release. No directories or files
are created, changed or repaired.

This is a sequential namespace observation, not an atomic snapshot of every ancestor
or alias. It must never supply write authorization or replace the stricter typed
publication paths. `show --effective` uses this reader for the selected manager's
fragment and drop-ins and applies display redaction separately.

Filesystem fixtures exercise relative/absolute/chained aliases, parent traversal
after alias expansion, writable intermediate directories, loops, final symlinks,
foreign-owned aliases when root, missing files, shared budgets and output preservation.
