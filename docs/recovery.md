# Transactions, editing and recovery

A reviewed change records a private transaction UUID before it can publish service
files. The scope lock serializes writers. An incomplete transaction reserves its
names and blocks further mutation in that scope until it is explicitly reconciled.
Read-only status, show, doctor and recovery inspection remain available.

## Inspect first

```sh
tired recover --json
tired recover --transaction TRANSACTION_UUID --resolution finish --yes --json
tired recover --transaction TRANSACTION_UUID --resolution rollback --yes --json
```

Add `--user` for the current user's scope. Inspect the selected UUID, recorded
operation, pending action, current unit/manager state, file fingerprints and
rollback material. `finish` continues only steps the evidence permits. `rollback`
restores the previous owned control-plane state. An unpublished preparation can
only be rolled back and reviewed again.

Intent without completion does not mean an action never ran. Recovery checks the
manager's current job and unit state. An admitted start/restart is never blindly
submitted again. A running process alone cannot prove that a restart or an edited
revision was activated. A pending activation needs the durable completion event
for its exact unit and accepted manager job, bound to its journal intent. Without
that evidence, recovery preserves the transaction and returns code 8 even if the
service is running. A recorded failed activation remains a failure; an immediate
edit restores the preceding revision and its requested runtime condition.
Application side effects cannot be undone by restoring a unit.
Reload and enablement are idempotent but still journaled. Foreign files, unsafe
ownership, unresolved jobs, missing evidence or a changed manager produce a
recovery-required result instead of an overwrite.

## Durable phases

The transaction contains a digest-bound request, the pre-operation state, a file
manifest, stage/backup inode ledgers, native job/enablement evidence and an append-only
sequence of atomic progress entries. New candidates are allocated and identified
before being linked or written. Every external action has a durable intent and a
completed, failed or uncertain result. Every publication verifies expected absence
or the exact expected owned file identity. Destination directories are synced.

Bootstrap recovery handles death before a complete request/manifest exists without
removing unrecognized entries. Cleanup has its own inode-bound deletion ledger,
which survives partial retirement of the request and journal. Interrupted cleanup
continues only on the exact previously recorded objects.

## Editing

```sh
tired edit relay --restart-sec 10s
tired edit relay --apply-mode defer --env ENDPOINT=https://example.invalid
tired edit relay --yes --json -- /opt/relay/bin/relay --config /etc/relay/app.toml
tired edit relay --refresh-profile
```

An active service normally applies edits by restarting. `defer` installs the new
configuration and environment reference while preserving the current process;
output labels that process as using an earlier start context. A later restart uses
the new revision. `--no-start` in immediate mode stops an active service. Explicit
finish recovery performs or reconciles the same stop and verifies an inactive unit
with no pending manager job before committing the edited configuration.

If an immediate edit fails startup or initial observation, tired restores the
previous unit/environment reference, enablement and previous active/inactive
condition. It revalidates the old executable, account and privilege facts before
starting it. Failure to prove restoration returns code 8. A successful control-plane
rollback returns code 6 and reports the restored identity/runtime.

Rollback does not restore application data, external configuration, live environment
files, credentials, journal entries or remote dependencies. A failed newly created
workload that may have executed remains installed for diagnosis and retry rather
than being silently erased.

## Drift and rename

`tired show NAME --effective` inspects the current fragment and applicable drop-ins.
External edits are not overwritten by default. An explicit `edit --restore-managed`
requires the `restore-drifted-unit` acknowledgment and saves foreign unit bytes in
private history before replacement. Applicable drop-ins still require native
review; tired does not guess their merged execution semantics.

Rename retains the service UUID, stops and disables the old unit, creates the new
name, observes its requested runtime, and then retires the old file. The controller
prevents simultaneous duplicate processes. Failed rename restores the old identity
and previous intent. Former names remain evidence for log inspection.

Default history retains five prior revisions; configuration permits 1–1000 with a
64 MiB aggregate read/retention budget. Immutable environment revisions are pruned
only when no active or retained revision references them. Removal keeps a minimal
private receipt, retaining full private history only with `--keep-history`.

See [exit codes](cli.md#exit-codes), [inspection details](recovery-inspection.md)
and [ownership safeguards](security.md).
