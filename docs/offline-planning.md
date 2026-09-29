# Offline generic planning

Run `tired plan --offline -- /absolute/program argument` to construct
a read-only proposal. `--json` emits one structured document, while `--unit` emits
a unit view. Installed profiles are matched automatically. `--profile none` disables
matching; `--profile ID` selects a profile explicitly for renamed workloads. Live
planning remains unfinished.

Planning first discovers and validates administrator and invoking-user settings as
described in [Settings](settings.md). Malformed or unsafe settings fail explicitly,
including when profile matching is disabled. Configured retry policy and restart
delay replace generic defaults; compatible profile recommendations can replace
configured values, while explicit CLI choices take precedence over both. JSON
reports `administrator-config` or `user-config` field origins. Text summaries show
the effective retry policy and restart delay origins.

Preparation captures the command once, resolves account/group choices, applies typed
CLI overrides, recomputes dependent retry/scope/name defaults, imports only selected
environment inputs, and derives private environment revision paths. Relative input
paths resolve against the invocation directory. User-scope requests cannot switch
accounts or groups. System-scope state paths never use XDG variables.

No service files, environment revisions, manager operations, or target execution
occur. Generated names are tentative and UUIDs identify this proposal only. Output
says `live_validation: not_performed`, `collision_check: not_performed`, and
`replayable: false`; it is not privileged helper input or evidence of service health.

JSON fields preserve origins and typed values. Durations are integer microseconds,
CPU quotas are hundredths of a percent, permission modes are numeric bitmasks, and
infinite limits use the string `infinity`. Inherited/unset fields omit a value.
Environment entries have independent origins and sensitivity classifications.
Use repeatable `--sensitive-arg INDEX` to classify arguments that the heuristic does
not recognize. Index 1 is the first workload argument after the executable; index 0
and nonexistent arguments are errors. For example:

```console
tired plan --offline --profile none --sensitive-arg 2 -- /opt/service login private-value
```

The classified argument is masked in text, unit previews and JSON field values.
JSON retains the explicit indices in `sensitive_argument_indices`. Classification
does not alter the actual workload argument, remove secrets from its process
arguments, or make the eventual unit metadata private. It raises the
`sensitive-command-data` risk. The same option works with `profiles explain`.
Repeated classification of one index is harmless. Private authorized exports may
reveal classified arguments; ordinary previews remain labeled non-installable when
redacted. Classifications are currently retained in the proposal; persisted service
records and editor handling are still pending.

The [risk inventory](risks.md) reports identified risks and pending privileged-code
inspection separately; it does not claim approval or installation readiness.

Default output masks recognized password/token/secret/api-key command flags and
classified environment values. Redacted unit views are labeled non-installable.
The internal command remains unchanged. This heuristic is not complete secret
detection. C0/C1 terminal controls are escaped; JSON strings retain their meaning.

`--output NEW_FILE` creates a private file without overwriting existing paths.
Unredacted export additionally requires `--include-sensitive --allow-risk
sensitive-export`. Nothing unredacted is sent to stdout by that option. An export
failure may leave a private partial file and reports this explicitly. The only
persistent planning effect is the explicitly requested output file.

The current executable also provides help/version, profiles list/show/explain/validate, and
config show/validate. Profile installation/removal, creation, live plans,
dashboard and management handlers remain under implementation and return
an explicit unsupported result. Their parser recognition is not implementation
completion. Full risk validation, host compatibility, real execution round trips,
and release qualification remain required.
