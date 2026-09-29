# Inspecting stored transactions

`tired recover` inspects stored system-scope transaction journals. Add `--user`
for the current user's scope and `--json` for structured output. Inspection does
not create state directories, acquire a mutation lock, change services, or launch
workloads. System private-state access may require running with administrative
authority; the command does not silently elevate or treat denied access as empty.

This implementation reports journal evidence only. Its output explicitly states
that live reconciliation was not performed and resolution actions are not yet
implemented. It does not determine the current manager outcome, verify manifests,
or offer finish/rollback as available actions. Those are required subsequent
parts of recovery implementation.

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
with ASCII hex escapes, including filenames that are not valid UTF-8.

Native tests cover empty-state inspection without creation, nonterminal progress,
JSON status reporting, invalid neighboring directory names, terminal-safe text and
preservation of prior output on discovery failure.
