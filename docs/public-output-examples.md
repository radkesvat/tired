# Public JSON output examples

[The fixture corpus](../tests/public_output/fixtures.json) records real command
outputs from a disposable systemd guest. It contains successful and unsuccessful
examples for every public command and the profile/configuration subcommands, plus
the offline/live plan, deferred edit, effective-unit, `--now`, runtime-failure and
selected recovery forms. The host description and build-info example identify the
environment used for capture. UUIDs, PIDs, timestamps, paths and manager properties
are observations from that guest, not values to copy into a new request.

Each case contains its name, exact argument vector, observed process `exit_code`,
verbatim `stdout` and `stderr`, and a boolean `ndjson` flag. Parse `stdout` as one
JSON document unless `ndjson` is true. Logs emit one JSON object per line, including
access, record and completion/error events. Diagnostics and progress on stderr
are separate from the public machine output. Fixture bookkeeping is not part of
the CLI protocol.

The public documents and log events declare `schema_version: 1`. Within this major
version, consumers must allow additional properties. Command-specific required
fields and meanings are described by the installed
`machine-output.schema.json`, `operation-result.schema.json`, the
[command reference](cli.md), and the linked component references. Public output
is inspection data and cannot be submitted as a privileged helper request.

Mutation examples report installation, enablement and runtime separately. Code 0
means the requested operation completed; it does not establish application health.
Code 6 records a failed edit whose preceding control-plane revision was restored.
Code 7 records a failed/unconfirmed requested runtime, with installation preserved
where required. Code 8 records an incomplete reconciliation; it must not be treated
as ordinary success merely because a unit is running. Other refusal examples use
code 2 for invalid input or missing approval, code 5 for a collision, and code 9 for
a missing managed object. See the full [exit-code table](cli.md#exit-codes).
Unavailable facts remain null, omitted or explicitly unknown as documented; they
must not be converted to false, zero, stopped or healthy.

The corpus is an example set of command forms. It does not promise to demonstrate
every possible transport failure, authorization policy or flag combination.
The CTest check parses every captured output, checks schema majors and any reported
exit code, and requires all registered examples exactly once. It never changes
services. Regenerating the corpus exercises actual operations and is restricted to
a disposable guest with the root-owned `/opt/tired-tests/disposable` marker and
the native fixture installed:

```text
tired_public_output_fixtures record ABSOLUTE_FRONTEND ABSOLUTE_SOURCE NEW_OUTPUT_FILE
tired_public_output_fixtures validate OUTPUT_FILE
```

The recorder uses only `tired-qualification-output*` service names and the
`tired-qualification-output-profile` local profile. It checks ordinary success,
conflict, missing-object and approval-refusal paths. Recovery examples interrupt
the real controller after durable preparation, then invoke the public inspection
and explicit finish/rollback commands. Cleanup outputs are retained as cases so
the corresponding fixture removal is part of the recorded execution.
The new output path must not exist; a failed run retains its state for diagnosis.
