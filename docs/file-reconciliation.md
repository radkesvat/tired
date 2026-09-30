# Comparing transaction destinations

`tired_file_reconcile` validates a manifest and compares each typed destination
with its before and after fingerprints. It derives paths from the trusted layout
and rejects a scope mismatch before replacing any prior output. Results preserve
manifest order and contain metadata/digests, never file contents.

Each entry has one state:

- `BEFORE`: actual identity, metadata and content match the recorded before-state.
- `AFTER`: they match the recorded after-state.
- `FOREIGN`: inspection succeeded but neither fingerprint matches.
- `UNKNOWN`: inspection could not establish a trustworthy observation; the entry
  retains the specific error.

Missing files or parent components reached through trusted traversal establish
absence. Permission errors, unsafe parents, symlinks and nonregular or multiply
linked files do not. A failure on one destination preserves observations for its
neighbors. `complete` means every entry was inspected; `foreign` independently
reports any known mismatch. Neither flag establishes authorization to mutate.

Each fingerprint is limited to 16 MiB. A manifest has a 64 MiB content-read budget;
successful reads charge their actual size, failed reads conservatively charge
their entire allowance. Fingerprinting may additionally read one byte to detect
growth beyond its allowance. Exhausted entries remain unknown. No file bytes are
retained after hashing.

These are sequential observations and may become stale. The controller must bind
the manifest to the journal and approved request, verify rollback/staging artifacts,
and revalidate under the scope lock before mutation. The transaction controller
performs these checks for explicit CLI finish/rollback actions; the observation
component itself remains read-only.

Native tests cover absence, after-state matches, metadata drift, unknown symlink
state alongside a valid neighbor, missing parents and atomic scope rejection.
