# Private service records

The schema-1 record composes identity/revision metadata, the complete typed model,
private environment and credential references, historical review evidence,
last-inspected executable identity and an optional profile snapshot. It also stores
the installed unit path, optional owned environment revision UUID/path/digest and
known external application configuration paths. External environment paths remain
in the typed model; credential references remain in the private input snapshot.

The record preserves requested start/enable choices and per-field origins rather
than treating them as current manager state. Live status is queried separately.
No independently authoritative name-to-UUID index is part of this format.

Native parsing validates each nested codec and their relationships: model name and
scope match metadata, executable lexical paths agree, argv count matches review,
and nonempty managed environment values require a revision reference. System
records cannot cite a user-origin profile as their selected source. The model must
be complete enough to render a unit and must retain explicit start/enable choices.
Profile absence is represented as null, without fabricating generic provenance.

`tired_service_record_check_layout` compares unit/environment paths with destinations
derived from the trusted selected scope layout and service/revision identifiers.
Recorded paths remain evidence, not arbitrary mutation destinations. The controller
must perform this check and filesystem/ownership verification before use.

Digest syntax is validated, but parsing does not compare a live unit or environment
file with its recorded digest. It also does not authenticate review evidence,
re-resolve executable paths or query accounts. Those checks and request approval
remain necessary before mutation. Rendering during validation checks model
completeness; its bytes do not replace the recorded historical unit digest.

The format contains unredacted private argv/environment data. Use owner-0600 files
in owner-0700 state directories and apply existing sensitivity rules to display.
Input/output are limited to 16 MiB, with stricter nested codec and JSON structural
limits. Encode/parse failures preserve prior owned output.

`tired_service_record_load` opens the selected layout's existing private records
directory and reads only a canonical `<uuid>.json` filename. It applies private-file
owner/mode/single-link/no-follow checks, then binds the embedded UUID, scope and
owner to the requested record and checks derived paths. Missing records return
not-found; inaccessible or unsafe storage is never treated as empty. No directories
are created or repaired. The descriptor-based read variant permits inventory code
to reuse an already trusted records directory associated with that layout.

Native tests compose and round-trip the components, exercise optional profile and
environment references, verify trusted-layout agreement, and reject mismatched
review counts and missing environment references. Filesystem fixtures cover trusted
loading, renamed UUID files, symlinks, unsafe permissions, layout mismatch and
failure preservation. Transactional record publication, inventory discovery, record
creation from a reviewed plan and ownership-aware status remain integration work.
