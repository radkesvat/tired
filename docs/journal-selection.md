# Selecting journal records

`tired_journal_selection_build` constructs bounded native sd-journal match clauses
for one full safe service name and scope. Optional boot selection is an exact
32-digit lowercase boot ID. User-scope callers must supply the authorized selected
UID; this pure builder does not authorize access to another user's logs.

System scope includes:

- Application records with trusted `_SYSTEMD_UNIT` equal to the selected unit,
  regardless of the service process's UID.
- Manager records with `UNIT` equal to the selected unit and trusted `_UID=0`,
  `_PID=1`, and `_COMM=systemd` origin fields.

User scope includes:

- Application records with trusted `_SYSTEMD_USER_UNIT` equal to the selected unit
  and `_UID` equal to the selected user.
- Manager records naming the service in `USER_UNIT`, with the selected `_UID`,
  `_SYSTEMD_UNIT=user@UID.service`, `_SYSTEMD_USER_UNIT=init.scope`, `_COMM=systemd`,
  and `_EXE` identifying `/usr/lib/systemd/systemd` or `/lib/systemd/systemd`.

Those executable locations cover the declared baseline distributions. An unrecognized
manager origin is excluded rather than falling back to `USER_UNIT` alone. Matching
depends on journal-generated underscore fields, not application-supplied claims of
those fields. It is not cryptographic authentication of arbitrary imported journal
files. Coredump/other-daemon association records are not included by these clauses.

Each clause ANDs distinct fields, and the clauses are ORed. The boot restriction,
when present, is included in every clause so no manager branch bypasses it.
The same built selection can check decoded native field values with exact byte
comparisons. Missing or binary-mismatched identity fields do not match.

Apply a selection to an exclusively owned new journal handle before iteration.
Match installation does not flush existing filters. If any native match operation
fails, the caller must close the handle without reading partially configured results.
This component neither opens production journals nor expands filesystem permissions.

Tests cover same-name user isolation, nonroot system-service records, trusted manager
origins, forged UNIT/USER_UNIT payloads, boot restrictions and malformed selectors.
A real sd-journal handle over an empty private fixture verifies native match setup
without writing to or iterating the host journal. The logs frontend combines this
selection with access diagnostics, bounded entry decoding and tail/follow polling.
