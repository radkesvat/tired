# Command parsing

The parser copies argv into an owned request without touching files, reading the
environment, invoking a manager, or executing a workload. Requests publish only on
successful parsing. Frontend arguments are bounded by 4096 entries plus argv[0] and
1 MiB including terminators.

The first workload token ends frontend option parsing. Every following token,
including empty strings and flags named `--help` or `--json`, belongs to the workload.
`--` creates an explicit boundary. Reserved subcommand tokens take precedence at
initial dispatch; use `./status`, `-- status`, or `create -- status` for a workload
whose name is reserved. No PATH lookup affects dispatch.

Creation named options map to the typed registry. `--set FIELD=VALUE` uses the same
field IDs, so mixing the two forms does not bypass duplicate scalar detection.
Collection options append. Explicit environment assignments merge according to their
own precedence rules. `--nofile SOFT:HARD` assigns both fields. `--system` and `--user`
conflict, as do opposing activation flags. Boolean flags reject attached values.
Both `--name value` and `--name=value` are accepted for value-taking options.

Working-directory and file arguments are retained for resolution against captured
context later. Parsing never captures `--pass-env` values or reads import files.
`create --dry-run` becomes a plan request. Offline mode is limited to plans; unit
and JSON output modes conflict. Sensitive export requires an output destination,
with sensitive-export acknowledgment enforced by the frontend.

The parser covers creation options, common flags, management filters/actions,
profile/config subcommands, command replacement and explicit recovery. Profile explanation
requires an explicit `--` before the workload and preserves all following arguments.
`--sensitive-arg INDEX` is repeatable for creation, planning and profile explanation.
It accepts existing workload argument indices after the executable, preserves the
argument bytes, and marks those indices for display redaction.
Other commands fail explicitly as unavailable. Successful parse recognition does
not imply that a corresponding service-management handler is complete.

`--unset FIELD` explicitly inherits an optional generated setting. It preserves user
precedence, so a profile cannot refill the cleared field. It cannot be combined with
an assignment of the same field in one request. Required command/identity fields and
policy selectors cannot be unset; semantic constraints are checked after merging.
