# Private typed model snapshots

The schema-1 ServiceSpec codec records every registered field with its origin,
explicit-inheritance flag and value. Unset and inherited fields have null values;
explicit inheritance is a separate user-origin state. Explicit empty lists remain
active list values. Parsing never recomputes defaults or profile recommendations.

Scalar values use canonical strings interpreted by the field registry: exact
microseconds with `us`, integer limits or `infinity`, octal modes, exact percentages,
numeric signals, literal text and canonical choice tokens. Booleans use `true` or
`false` strings. Ordered collections are string arrays, preserving literal argv
including empty arguments. Names are already normalized and do not lose another
`.service` suffix during parsing.

Native parsing requires exactly the registered field set and rejects extra/missing
fields, duplicate keys, invalid origins, inconsistent inheritance/value states and
wrong types. Values pass through typed model setters and scalar-combination checks.
Input and output are limited to 1 MiB; prior owned outputs survive failure. The
installed JSON Schema describes structure; native registry validation enforces
the complete field set and field-specific rules.

This is private unredacted storage and can contain sensitive arguments/text. It is
not a diagnostic format. Encode API-validated models and store snapshots through
private file APIs. Full service records must additionally preserve identities,
environment revisions, credentials, profile evidence, approved risks, timestamps
and file/transaction identities; this codec does not stand in for those records.

Tests cover defaults/unset values, field origins, explicit inheritance, empty lists,
literal argv, normalized suffixes, exact large limits, durations/timeouts, quotas,
modes, canonical re-encoding and malformed-input output preservation.
