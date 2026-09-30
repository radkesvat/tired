# Command reference

`tired COMMAND [ARG...]` and `tired create [OPTIONS] -- COMMAND [ARG...]` capture a
literal command vector. The first workload token ends frontend parsing; all later
flags belong to the workload. Use `--` for a workload named like a subcommand or
starting with `-`. No quoting layer, expansion, globbing or shell evaluation is
added. Your invoking shell has already processed its own syntax.

## Commands

| Command | Behavior |
|---|---|
| `tired` | Dashboard with TUI enabled, a usable terminal and no `--json`; listing otherwise |
| `create [OPTIONS] -- COMMAND ...` | Review, authorize and install a persistent service |
| `plan [OPTIONS] -- COMMAND ...` | Read-only proposal; native validation unless `--offline` |
| `list` | Managed inventory; filters below |
| `status NAME` | Fresh manager state, stored identity and disk integrity; failure is data |
| `show NAME` | Saved model, profile snapshot, provenance and unit evidence |
| `logs NAME` | Bounded selected-service journal, including recorded former names |
| `start NAME` / `stop NAME` / `restart NAME` | Explicit current activation; scoped failure reset for activation of a failed unit |
| `enable NAME` / `disable NAME` | Boot activation policy; `--now` also starts/stops |
| `edit NAME [OPTIONS] [-- COMMAND ...]` | New checked revision; active services apply by restart unless deferred |
| `rename NAME NEW_NAME` | Checked identity transfer, preserving UUID/history |
| `remove NAME` | Stop/disable, remove owned resources, preserve application files |
| `doctor [NAME]` | Scope or service diagnosis with concrete corrective advice |
| `recover` | Inspect interrupted transactions and current evidence |
| `recover --transaction UUID --resolution finish\|rollback --yes` | Explicit selected reconciliation |
| `profiles list` / `show ID` | Inspect available declarative profiles |
| `profiles explain -- COMMAND ...` | Passive match/recommendation explanation |
| `profiles validate FILE` | Strict schema and semantic validation without a manager |
| `profiles install FILE --yes` / `remove ID --yes` | Explicit local profile changes; `--user` selects account config |
| `config show` / `validate FILE` | Effective settings/origins or read-only file validation |

Unit names may omit `.service`. Explicit names are exact and conflicts are errors.
Automatically generated names receive a checked suffix when occupied. Headless
automatic creation may choose a later suffix under the lock if a conflict appears
between review and commit. Interactive approval of a stale exact name fails safely.

## Common options

`--system` (default) and `--user` are mutually exclusive. User scope controls only
the current account. `--json` requests structured output; logs use one JSON object
per event. Human diagnostics and progress use stderr. `--quiet` suppresses routine
human result output; it does not suppress JSON. `--verbose` adds transaction detail.
`--color auto|always|never` controls presentation. `--help`, `--version` and
`--build-info` provide usage, version and compiler/linkage/baseline information.
`--help --json` emits one help document with `command: "help"` and the usage text
in its `usage` string, with the same versioned envelope as other JSON output.

`--no-tui` selects plain review and compact listing for a zero-operand invocation.
The effective `tui` [setting](settings.md) must be enabled for the interactive
[dashboard](list.md); `--json` selects structured listing regardless of terminal.
`--yes` approves ordinary noninteractive changes; it never bypasses validation,
authorization, ownership or individual risks. Use
repeatable `--allow-risk CODE` for [specific acknowledged risks](risks.md). A changed
high-risk choice requires a fresh acknowledgment for that choice. Noninteractive
creation/edit/remove without approval refuses promptly. Lifecycle commands are
explicit actions and need no additional ordinary confirmation.

## Creation and edit inputs

| Option | Meaning |
|---|---|
| `--name NAME`, `--description TEXT` | Exact safe name and unit description |
| `--working-directory PATH` | Resolve relative input against the invoking directory |
| `--run-as USER`, `--group GROUP` | Selected NSS account/group; user scope cannot switch identity |
| `--profile auto\|none\|ID` | Passive matching, generic proposal, or explicit profile selection |
| `--type exec\|simple\|notify\|forking\|oneshot` | Systemd service process model |
| `--start`, `--no-start` | Immediate activation choice |
| `--enable`, `--no-enable` | Boot activation choice, independent of current runtime |
| `--restart POLICY` | `no`, `on-failure`, `always`, `on-abnormal`, `on-success`, `on-abort`, `on-watchdog` |
| `--restart-sec DURATION` | Nonzero delayed retries in persistent mode |
| `--retry-policy persistent\|limited` | No finite rate budget or an explicit finite budget |
| `--start-limit-interval DURATION`, `--start-limit-burst INTEGER` | Limited-policy rate budget |
| `--network none\|network\|online` | Explicit ordering; online is not Internet readiness |
| `--nofile SOFT:HARD` | Paired descriptor limits within observed ceilings |
| `--env KEY=VALUE` | Explicit managed assignment; repeatable |
| `--pass-env KEY` | Capture only the named exported variable |
| `--import-env-file FILE` | Snapshot supported assignment grammar at review time |
| `--env-file FILE` | Live manager-readable reference, read on each activation |
| `--credential NAME=FILE` | Application-compatible systemd credential reference |
| `--sensitive-arg INDEX` | Explicit command-argument classification; argv index 0 excluded |
| `--enable-linger` | Explicit current-account persistence change, user scope only |
| `--hardening baseline` | Explicit `NoNewPrivileges=true`, `PrivateTmp=true`, `ProtectSystem=full` |
| `--set FIELD=VALUE`, `--unset FIELD` | Typed advanced field or explicit inheritance |

Duplicate scalar assignments, including a named flag plus `--set`, fail. Collection
flags append/merge according to their typed rules. Environment precedence within a
proposal is snapshot import, named pass-through, then explicit assignment. Managed
assignments override duplicate live-file values. Newly supplied edit values replace
historical values without inheriting their previous precedence rank. No whole-shell
environment capture occurs. See [environment grammar](environment.md).

The [complete field reference](model-reference.md) lists every advanced field,
type, directive, registry default and valid choice. [Model rules](model.md) define
its exact grammar and semantic limits.
Examples include `--set timeout_start=30s`, `--set timeout_stop=10s`,
`--set pid_file=/run/myapp/main.pid`, `--set supplementary_groups=adm`,
`--set memory_max=512M`, `--set tasks_max=128`, `--set cpu_quota=150%`,
`--set no_new_privileges=true`, `--set private_tmp=true`,
`--set protect_system=strict`, `--set read_write_paths=/var/lib/myapp`,
`--set after=network.target`, `--set state_directory=myapp`,
`--set kill_signal=SIGTERM`, and `--set success_exit_status=2`.
A list assignment supplies one literal member per repeatable assignment. There is
no raw directive or executable-hook interface.

The baseline hardening preset blocks privilege gains, isolates temporary files,
and makes `/usr`, `/boot` and `/etc` read-only. Review the proposed unit and the
application's write requirements. Mount namespace support is required; nonroot
user services cannot use this preset on the supported baseline. Ordinary defaults
are unchanged. Assigning a preset field again with `--set` or `--unset` is a
duplicate scalar error; use individual fields for a different combination.

In review, `H` displays the preset's settings and compatibility warnings; Space
selects it and Escape returns. `?` shows the selected field's effective value,
origin, recommendation disposition, rationale and cited sources. Editors retain
their pending values during resizing and block edits below their minimum size.

## Inspection and operation options

`plan --offline` works without a manager and labels live checks as unperformed.
`plan --unit` emits unit text; `create --dry-run` selects plan behavior.
`show --unit` prints the current unit view. `show --effective` inspects the native
fragment and applicable drop-ins; it does not synthesize a merged installable unit.
`plan` and `show` support `--output NEW_FILE`. Unmasked export additionally requires
`--include-sensitive --allow-risk sensitive-export` and is never streamed to stdout.

`list --active-state STATE --enabled-state STATE --profile ID --search TEXT`
filters known observations and case-insensitive names. Unknown observations remain
unknown. `status --check-active` returns 7 when the unit is not currently running.

`logs --follow --lines N --since TIME --boot current|BOOT_ID` selects bounded local
journal history and future events. See [accepted time forms](log-options.md).
Access denial is a specific diagnostic, not an empty journal success.

`edit --apply-mode restart|defer` selects immediate application or an explicitly
older running context. `edit --refresh-profile` reevaluates recommendations; ordinary
edits preserve the saved snapshot. `--restore-managed` explicitly restores managed
configuration over reviewed foreign edits with a backup and risk acknowledgment.
`remove --keep-history` retains private history. Application files, external inputs
and journal data are never removed. `enable|disable --now` also changes runtime.
Recovery accepts `--transaction UUID --resolution finish|rollback --yes`; ordinary
recovery inspection already collects live evidence for nonterminal transactions.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Requested operation completed |
| 1 | Internal/unexpected failure |
| 2 | Invalid arguments, model, profile or settings |
| 3 | Required environment/feature unsupported or unavailable |
| 4 | Authorization failed |
| 5 | Name, ownership, stale-review or concurrency conflict |
| 6 | Failed change restored its previous control-plane state |
| 7 | Installation exists but requested runtime failed or was not confirmed |
| 8 | Reconciliation or rollback remains incomplete; inspect recovery |
| 9 | Requested managed object/manager not found |
| 10 | Cancelled before commit |
| 130 | Interrupted before commit |

`status` returns 0 for a completed valid query even if its workload failed; use
`--check-active` for a running-state gate. Once an approved request transfers, an
interrupt waits for the bounded worker's recorded result instead of claiming no
mutation occurred. Result JSON separates installation, enablement, runtime,
rollback/recovery and unverified application health.

The installed `machine-output.schema.json` describes the public major-1 envelope;
`operation-result.schema.json` specifies mutation results. Public outputs permit
additive properties within the major version. The independently validated private
`helper-request.schema.json` is a transport contract, and public JSON is never a
replayable privileged request. Version and build-info outputs also declare major 1.
