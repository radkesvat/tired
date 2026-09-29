# Inspecting stored transactions

`tired recover` inspects stored system-scope transaction journals. Add `--user`
for the current user's scope and `--json` for structured output. Inspection does
not create state directories, acquire a mutation lock, change services, or launch
workloads. System private-state access may require running with administrative
authority; the command does not silently elevate or treat denied access as empty.

Valid nonterminal transactions also receive read-only live manager observations.
The CLI validates manager identity and version, then queries their units under a
five-second aggregate monotonic budget. Empty inventories, terminal records and
invalid entries do not trigger live queries. Unavailable transport, permission
errors and incomplete queries leave live state unknown while preserving stored
diagnostics. No activation or interactive authorization is requested.

Pending renames with validated manifests query both the new and old unit names.
JSON rows retain the new unit in `unit_name`/`live` and report the old unit in
`previous_unit_name`/`previous_live`; text output labels the old unit separately.
Requested/successful unit counts include both queries. Up to 1024 transactions
can therefore supply 2048 unit names, all sharing the same five-second deadline.

Manager observations are not complete recovery reconciliation. The command still
does not compare manifests and expected file identities or offer finish/rollback
actions. `live_reconciliation` remains `not_performed` until those checks exist;
`live_observations` separately reports `not_needed`, `completed`, `partial` or
`unavailable` for the selected units.

Rows identify stored progress, pending action, sequence and recorded uncertainty.
Invalid entries retain diagnostics alongside valid transactions. Journal state is
labelled as such and never substituted for current service runtime state. An intent
without completion must not be interpreted as proof that an action never ran.

Exit status is 0 when inspection finds no nonterminal transactions and the
inventory is complete. It is 8 when the inventory is incomplete or any transaction
remains nonterminal. Other inspection failures use their normal error status.
JSON includes `inventory_complete`, `recovery_required`, `live_reconciliation`,
`resolution_actions_supported` and an array of transaction rows. Valid rows include
the transaction UUID; all rows have a `directory_name_display` suitable for
diagnostics. Control bytes, backslashes and non-ASCII filename bytes are represented
with ASCII hex escapes, including filenames that are not valid UTF-8. Selected
rows have a `live` object with completion timestamp and either an error or typed
manager properties. Unknown fields are omitted, not replaced with zero, false or
stopped. Known zero values remain present. Queries are sequential snapshots and
may become stale immediately; their timestamps do not prove future state.

Native tests cover empty-state inspection without creation, nonterminal progress,
JSON status reporting, invalid neighboring directory names, terminal-safe text and
preservation of prior output on discovery failure. Live integration exercises the
CLI collector's identity/version/query path against the existing system manager;
output tests distinguish known zero from unknown and denied observations.
