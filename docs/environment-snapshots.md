# Private environment snapshots

The schema-1 snapshot contains environment entries (`name`, literal `value`,
`origin`, `sensitive`) and credential references (`name`, absolute `path`). It never
reads the process environment or credential files. Empty and multiline values,
equals signs and spaces retain their literal meaning; no shell interpretation
occurs. Input order and origins are preserved.

This format is unredacted private storage. Ordinary display must use the existing
redaction interfaces instead. Credential contents are not copied into the snapshot.
Native parsing rejects unknown fields, duplicate names, invalid assignments, invalid
paths and sensitivity flags weaker than the environment model requires. Credential
name/path components are checked after assignment validation so embedded separators
cannot silently change their interpretation.

Parsing replaces environment and credential outputs together only after the entire
snapshot passes validation. The codec permits up to 8 MiB serialized JSON to allow
escaping of bounded model values; existing per-container entry/byte limits and
strict JSON structural limits still apply. The installed schema describes shape;
native environment/credential APIs enforce detailed naming and size rules.

Tests cover empty snapshots, literal values, origins, sensitivity, credential paths,
stable re-encoding, duplicate variables, sensitivity downgrades, embedded name
separators and preservation of both outputs on failure. Full service records still
need environment revision identities/digests and other provenance alongside this
snapshot.
