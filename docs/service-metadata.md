# Service identity and revision metadata

The schema-1 metadata codec holds the identity portion of a private service record:
service UUID, full unit name, current revision UUID, last transaction UUID, unit
content digest, writer version, creation/update timestamps, numeric owner/invoker/
service identities, scope and interactive/headless creation mode.

UUIDs are canonical version-4 identifiers and digests are lowercase SHA-256 hex.
Timestamps are nonzero unsigned microseconds; update time cannot precede creation.
UID/GID values retain their numeric identities without string/float conversion and
exclude the platform's invalid-ID sentinel. System records require owner UID 0;
user records require matching owner, invoking and service UIDs. Group IDs are
recorded separately and need not equal account primary groups.

Strict parsing requires all 16 fields and rejects extras, malformed identifiers,
unsafe unit names, invalid identity relationships and out-of-range values. Input
and output are bounded to 4096 bytes; owned output is replaced only on success.
The installed schema describes field structure; native validation also enforces
cross-field scope and timestamp relationships.

These values are descriptive historical evidence. They do not authenticate a
caller, prove current file ownership or replace account-database/manager checks.
The enclosing [service record](service-records.md) adds the typed model,
environment/revision data, profile evidence, risk approvals and captured executable
evidence. This metadata
alone must not authorize modifying a unit or advertise a complete installation.

Tests cover both scopes, distinct service/group identities, exact large timestamps,
canonical encoding, ownership mismatch, invalid timestamps/digests/ID sentinels,
unknown fields and preservation after failed parse/encode.
