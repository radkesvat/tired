# Listing services

```console
tired list
tired list --user --json
```

`list` discovers current records in the selected scope and performs one batch of
fresh manager queries. Text output shows name, scope, active/substate, enablement,
saved service UID, profile ID, unit/environment file state and pending transactions.
JSON includes each record's status evidence and diagnostics. No captured arguments,
environment values or credentials are exposed.

Rows remain visible for missing units, changed files, failed processes, conflicting
records and malformed filenames. Raw filenames are represented with lossless ASCII
escapes. A record with a duplicate unit-name claim is not accepted as a current
ownership authority. Valid neighbors of corrupt records can still be inspected.
Names reserved by incomplete create/rename/remove transactions also appear when no
current record exists; both names of a pending rename are included. Duplicate
reservations produce one extra row per name. They receive fresh live queries too.

Records retain raw filename order. Transaction-only names follow in journal order.
The report is read-only, not an atomic snapshot of all records and manager state.
It does not discover or adopt arbitrary units through copied ownership comments,
repair missing records, refresh profiles or resolve incomplete transactions.
Use `status NAME` for detail and `recover` for journal inspection.

Discovery is bounded to 1024 record entries and 1024 transactions. The combined
manager batch supports at most 3072 names under a shared five-second deadline.
Record discovery, record reloads and file inspection each have separate 64 MiB
read budgets. Private record contents are released after each row; the final text
or JSON output has an 8 MiB limit. Missing directories are not created.

A complete listing returns zero even when services are failed, missing or drifted.
Uncertain storage observations return 8; otherwise failed manager queries retain a
nonzero query status. Partial rows remain in the output. A complete empty inventory
returns zero without opening a manager connection. Pending transactions are explicit
states; they do not alone make read-only listing fail. An inaccessible records root
fails with its trust/access diagnostic rather than claiming an empty inventory.

Listing inherits status's current limits: it does not enumerate effective drop-ins,
prove loaded bytes equal disk, or report account lingering. Run-as identity is the
saved numeric UID, which does not depend on a potentially changed account name.
