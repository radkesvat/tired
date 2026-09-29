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
Durable journal storage and that controller are still separate unfinished work.

The installed JSON Schema documents the structural format. Native tests cover
round trips across action and outcome vocabularies, scope retention, malformed
IDs/digests/names, version/sequence bounds, missing/extra/duplicate fields and
failure preservation. Strict numeric parsing remains authoritative in the codec.
