# Discovering current service records

`tired_service_inventory_load` scans the trusted layout's private records directory
without creating or repairing it. Entries are sorted by raw filename. Each canonical
UUID filename passes the full trusted record loader; malformed names, corrupt files,
unsafe storage and identity mismatches remain per-entry diagnostics. Valid neighbors
remain visible. Only metadata is retained; private argv/environment snapshots are
released after validation.

Discovery accepts at most 1024 directory entries and shares a 64 MiB read budget.
Successful reads charge actual bytes, including records that later fail parsing.
Failed reads conservatively charge their full allowance. Exhaustion produces explicit
entry errors. The bounded reader also supports callers with smaller shared budgets.

Records that claim the same full unit name are all marked conflicting. An inventory
with any entry error is incomplete; name resolution then refuses to establish unique
ownership, since an unreadable record could conceal another claim. Read-only listing
can still show valid metadata and diagnostics together. Raw filenames must be escaped
before display, including control and invalid UTF-8 bytes.

A missing records root gives a complete empty inventory. Other root errors propagate
and preserve the previous owned output. Observed directory namespace changes reject
the scan. This is not an atomic snapshot against in-place edits across multiple
records: callers reload the selected UUID and revalidate files under the scope lock
before mutation. No independently authoritative name index is written.

Filesystem tests cover empty/valid inventories, lookup, corrupt neighbors, duplicate
unit claims, raw malformed filenames, trust-failure output preservation and shared
read-budget exhaustion. The controller handles publication/cleanup; list and status
consume this inventory without interpreting it as current runtime state.
