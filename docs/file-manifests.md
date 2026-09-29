# Transaction file-change manifests

A file-phase manifest pairs typed targets with before/after fingerprints and
staging/rollback references. Its `prepared` anchor is the transaction's sequence-1
`prepare/completed` record, including the approved-request digest. All file targets
inherit that service UUID; the serialized target cannot supply another UUID,
absolute destination, directory root or permission mode.

Each change has exactly five fields: `target`, `before`, `after`, `staging_uuid`
and `rollback_uuid`. An existing after-state requires a staging UUID identifying
`.tired-<uuid>.tmp` in the resolved destination directory. An existing before-state
requires a rollback UUID identifying `artifacts/<uuid>` within the private transaction.
Absent states require null references. No-op absence/absence entries and duplicate
logical targets are invalid. Before/after existing files must have distinct inode
identities, reflecting atomic replacement rather than an in-place rewrite.

After-state modes are fixed by target role: `0644` units and `0600` environment or
record files. Before-state metadata describes what must be observed and does not
itself establish trusted ownership. Environment revisions are immutable: a change
may create a new revision or remove one, but never replace an existing revision
under the same UUID. Reference-aware pruning is separate from edit preparation.

Native validation enforces these file-plan shapes:

| Operation | File changes |
| --- | --- |
| Create | Add the named unit and its service record; optional new environment revisions |
| Edit/restore | Replace the named unit and record; optional new environment revisions |
| Remove | Remove the named unit and record; optional owned environment revisions |
| Rename | Remove one old unit, add the anchor's new unit name, replace the record; optional new environment revisions |
| Start/stop/restart/enable/disable | No unit/environment changes; optionally replace the service record |

This describes the file phase, not the full operation. Manager jobs, enablement
links, required observations and complete validated request data need associated
records and controller checks. A valid rename manifest provides both unit names
for reservation discovery, but inventory integration is separate work.

The strict schema-1 codec permits at most 256 changes and 1 MiB input, rejects
unknown/missing fields and invalid references, and preserves previous outputs on
failure. Nested progress records and fingerprints use their existing codecs.
The installed JSON Schema describes structure; native cross-field validation also
checks operation shape, target uniqueness, immutable revisions and references.

Parsing does not prove approval, artifact existence or current file identity. Before
any mutation the controller must load from trusted private storage, bind the anchor
to the journal and approved request, verify staging/rollback bytes and fingerprints,
resolve targets through the trusted scope layout and revalidate under the scope
lock. Manifest persistence, comparison and application remain integration work.

Tests cover create/edit/remove/rename round trips, restore validation, lifecycle-only plans, arbitrary-path
rejection, service mismatch, duplicate targets, immutable revisions, missing
references, incorrect modes/in-place identity, and failure preservation.
