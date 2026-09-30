# Showing saved services and installed units

```console
tired show relay
tired show relay --user --json
tired show relay --unit
tired show relay --effective --json
tired show relay --effective --unit
tired show relay --unit --output ./relay-redacted.unit
tired show relay --unit --include-sensitive --allow-risk sensitive-export --output ./relay-private.unit
```

`show` requires an unambiguous current service record in the selected scope. It
reloads that UUID, verifies its name/scope/path bindings, and reads the installed
unit through the trusted layout with a stable no-follow regular-file snapshot.
The unit view comes from those bytes, including foreign edits and comments. It
is never a newly rendered substitute for the installed file.

Default output includes saved metadata, typed model fields and origins, explicit
inheritance, environment values with sensitivity/origins, profile ID/revision/source
provenance and installed-file evidence. SHA-256, file owner/mode and UUID header
agreement describe the same bytes used for the unit view. Drift remains visible;
it does not authorize repair. JSON includes the unit text as a string. Text output
places the saved data and evidence before the multiline installed-unit view.

`--unit` shows only the installed unit view. Classified arguments and environment
assignments are masked using the shared preview policy and saved sensitivity.
Inline credentials are masked. Changed views are labeled non-installable. Terminal
output visibly escapes C0/C1 controls except ordinary newlines and tabs, adding an
escaping label when necessary. JSON escapes terminal C1 controls too.
See [unit-value parsing and redaction](unit-value-parsing.md) for classification
boundaries; unknown secret forms are not guaranteed to be recognized.

`--output` creates a new 0600 file and refuses existing files and symlinks. Redaction
remains the default. Unredacted export requires both `--include-sensitive` and
`--allow-risk sensitive-export`, and always requires that private output path.
`--yes` does not substitute for this acknowledgment. With authorized `--unit`
export the written bytes exactly match the stable installed-file snapshot, including
line endings and controls. Stdout receives only an export receipt. A failed write
can leave a partial private file; it is not blindly removed by pathname.

A saved record can be shown when its unit is missing. Ordinary `show` reports that
state and returns zero. A read/trust/redaction failure keeps the saved-model report,
marks the unit unknown, and returns 8. `--unit` instead returns the underlying error
when no safe unit view is available, including 9 for absence. Record discovery or
identity conflicts fail before displaying a selected model. Corrupt neighboring
records prevent an unambiguous lookup. Ordinary show makes no manager requests.

`--effective` queries the selected manager for its fragment and ordered drop-in
paths, then displays each current file separately. It may call LoadUnit to make an
unloaded configuration available in manager memory. It never starts/stops services,
enqueues jobs, enables units or requests a reload. Existing loaded configuration is
not refreshed by this call. Native queries share a five-second deadline.

The effective view contains up to 257 files (fragment plus 256 drop-ins) with a
shared 16 MiB read budget and a 4 MiB bound per file. Trusted directory aliases such
as merged-`/usr` paths are resolved with the [observed-file reader](observed-files.md);
unsafe paths and final symlinks are refused. Files must be valid UTF-8 text for this
view. Redaction and private-export authorization follow the ordinary show rules.
JSON retains reported/resolved paths, digests, redaction flags and per-file errors;
text identifies each source before its contents. A readable neighbor remains visible
when another file is missing or cannot be safely displayed. Unknown fragment/drop-in
properties are not interpreted as an empty configuration. Incomplete effective
views return 8, including when exporting a partial inspection.

`--unit --effective` is an annotated file inspection, not a merged installable unit.
With sensitive export authorized, each file's text remains unredacted, but the text
export also has file-boundary headings. Use ordinary `--unit` for an exact single-file
export. Configuration lookup and disk reads occur at different instants; file text
does not prove which bytes the manager previously parsed. The report exposes lookup
submission/acknowledgment separately from successful complete observations.

The unit read is limited to 4 MiB and formatted output to 32 MiB. This is historical
model plus current disk evidence, not a replayable installation request or proof of
what systemd currently has loaded. Effective inspection shows file boundaries rather
than inventing merged directive semantics. Review provides model/old/disk comparisons;
`status` reports fresh manager properties and drop-in paths separately.
