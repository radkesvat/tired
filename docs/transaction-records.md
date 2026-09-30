# Transaction progress records

Schema 1 progress records describe one action at one point in a transaction. Each
record carries the transaction UUID, service UUID, full unit name, scope, operation,
approved-request SHA-256 digest, sequence number, action, and action state. The
digest refers to the validated request/manifest; parsing the string does not compute
or verify that digest. Expected file identities, old/new manifests, rollback bytes,
manager job identities and observations belong in the transaction's associated
records, not in this compact progress envelope.

| State | Meaning |
| --- | --- |
| `intent` | The action is about to be attempted. After a crash, its effects may already exist. |
| `completed` | The controller recorded the required completion evidence for this action. |
| `failed` | The controller observed a failure; partial external effects may still exist. |
| `uncertain` | The outcome could not be established. Observation/reconciliation is required. |

Actions cover preparation, file publication, reload, enable/disable, start/stop/
restart, observation, current-record storage, file removal, rollback and commit.
The operation identifies the overall user request; the action identifies the
particular step. A completed publication does not imply a running service, and a
failed start does not imply that installation was undone. An intent record without
completion is never evidence that the action did not execute.

The native codec requires exactly the ten schema fields, schema version 1,
canonical lowercase UUIDv4 identifiers, a lowercase 64-digit SHA-256 value, a safe
full `.service` name, and closed scope/operation/action/state vocabularies. Sequence
numbers are 1–4096 and records are bounded to 4096 bytes. Duplicate JSON keys,
unsupported versions, invalid numeric types and unknown fields fail. Encoding
uses the same validation. Failures preserve existing owned output.

The codec does not authorize operations or validate the transaction state machine.
The journal/controller must enforce sequence continuity, stable identities/digest,
allowed action order, evidence for completion, and durable intent before external
effects. Recovery must compare records with actual filesystem and manager state
before deciding whether an uncertain action can be finished or rolled back.
The controller combines these records with manifests and native evidence for
explicit reconciliation; it never treats intent as proof of no effect.

The installed JSON Schema documents the structural format. Native tests cover
round trips across action and outcome vocabularies, scope retention, malformed
IDs/digests/names, version/sequence bounds, missing/extra/duplicate fields and
failure preservation. Strict numeric parsing remains authoritative in the codec.

## Durable journal storage

A dedicated private journal directory contains immutable `0001.json` through
`4096.json` progress records. Reading requires a contiguous sequence whose file
names and embedded numbers agree. Every record must retain the same transaction
and service UUIDs, unit name, scope, overall operation and approved-request digest.
Associated manifests and evidence belong outside this dedicated directory.

Reading never creates or cleans files. It rejects unknown directory entries,
unsafe record files, malformed content, gaps and identity mismatches. Recognized
UUID-based publication staging names are counted separately, never replayed as
progress. At most 128 such entries are accepted. They remain untouched for
inspection. Failed reads preserve the caller's previous journal snapshot.
The reader uses an independent directory stream, validates each file, and rejects
observed directory changes during the scan. This does not make a read-only scan
an atomic snapshot against noncooperating in-place edits.

Append requires the held scope lock, rereads the history, validates the next
sequence and stable transaction identity, and publishes the encoded record through
the atomic no-overwrite publication API. It refuses unexplained staging with
`journal-staging`/recovery-required. The caller receives the publication handle
even on a partial failure: inspect its published/durable flags, retry directory
sync if appropriate, or explicitly discard unpublished staging and report cleanup
failure. Never perform the external action until its intent is durably recorded.

This layer checks storage structure, identity continuity and the common progress
transitions below. It does not decide whether an action is authorized, follows the
operation-specific plan, is observed complete, or is safe to repeat. Manifest validation, pending-transaction
inventory discovery and live recovery still belong to the transaction controller.
Native journal tests cover append/readback, duplicate and mismatched appends,
missing/corrupt/misnumbered records, changed identities, unexpected entries,
symlinks, unpublished staging and preservation of earlier valid output.

## Progress transitions

Journal read and append both apply the same transition checks. The first record
is sequence 1, `prepare/completed`: the controller has durably prepared the
approved transaction and associated manifests. Preparation cannot recur in that
journal. External service effects require later intent records.

An ordinary action begins with `intent` and remains pending until a matching
`completed` or `failed` record. `uncertain` retains the pending action, including
when uncertainty is reported repeatedly. While pending, a different action,
another intent, rollback entry or commit is rejected. Read-only reconciliation
can supply evidence for a matching outcome; it must not blindly repeat the action.
Even a pending intent without an explicit `uncertain` record has an unknown
post-crash outcome. The explicit uncertainty flag distinguishes recorded doubt,
not permission to retry an intent that lacks it.

`rollback/intent` enters rollback mode only when no action is pending. Inverse
actions then have their own intent/outcome pairs. `rollback/completed` ends that
mode only when no inverse action remains pending. Failure and uncertainty during
rollback belong to its concrete actions; the rollback marker itself is a begin/end
boundary. A forward commit is forbidden during rollback.

`commit/intent` and its matching completion end forward progress. Committed and
rolled-back journals are terminal and accept no further records. A recorded
failure remains visible in the progress summary even if later steps succeed.
Known runtime failure does not automatically force rollback: creation can observe
the failed service and retain its installed configuration under the required policy.

These common checks prevent structurally invalid histories; they do not establish
the evidence behind completion or enforce each operation's required steps. The
controller must still validate the full approved plan and distinguish runtime
failure from installation failure. Native tests cover unresolved starts, invalid
commit attempts, matching reconciliation outcomes, inverse-action journaling,
terminal boundaries and invalid transitions injected directly into stored history.

## Pending transaction inventory

The inventory reads `<transaction-uuid>/journal` entries below a validated private
transactions root, without creating or removing anything. It sorts entries by raw
directory name and retains per-entry diagnostics, so one invalid transaction does
not hide valid neighbors. Raw names must be escaped before terminal/JSON display.
The directory name must match the canonical UUID in its first record and the
record's scope must match the scope being inspected.

A structurally valid nonterminal journal reserves its unit name. Committed and
rolled-back journals do not reserve names. Missing/empty/corrupt journals, scope
or identity mismatches, unexplained staging and exhausted scan budgets make the
inventory incomplete. The reservation accessor rejects an incomplete inventory
with recovery-required; partial results must never establish name availability.
Pending renames load their private `files.json` and require an exact match with
the journal's preparation record. Both the removed old unit and the created new
unit are reserved. Missing, invalid or mismatched manifests leave discovery
incomplete. The inventory entry retains the old name as `previous_unit_name`.

Discovery accepts at most 1024 transaction entries and a 16,384-record work budget.
Before reading another journal it reserves room for that journal's full 4096-record
bound; a successful read charges its actual count, while a failed read charges the
full bound. Rename manifests are read once per entry, bounded to 1 MiB and 256
changes; binding reuses the validated journal anchor rather than scanning it again.
Exhausted entries retain a diagnostic. Root namespace changes observed
during discovery reject the snapshot. This is not an atomic snapshot against
noncooperating edits inside previously read journals: commit must rescan under the
scope lock and verify manifests and actual state.

Tests cover empty inventories, unfinished-name reservations, terminal exclusion,
scope mismatch, missing/empty neighboring journals and the refusal to treat an
unfinished rename's single name as a complete reservation inventory. Tests also
cover both-name reservations and manifest digest mismatch. Recovery choices and actual file-state reconciliation are described in
[the recovery guide](recovery.md).

The layout loader opens the configured transactions location using the scope's
owner and private-directory checks. Only a missing path becomes a complete empty
inventory, without directory creation. Permission, type, link and trust failures
propagate; malformed entries remain an incomplete inventory. A failed load
preserves the prior output, which callers must not reuse as current evidence.
Layouts supplied to this internal API must come from trusted layout resolution,
never arbitrary privileged-helper request paths.
