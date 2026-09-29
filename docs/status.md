# Service status

```console
tired status relay
tired status relay.service --user --json
tired status relay --check-active
```

`status` reads current systemd state and saved installation information. It never
starts, loads, reloads, adopts or repairs a service. System scope is the default;
`--user` selects the invoking user's manager and XDG state/configuration locations.

User-scope status also observes the invoking account's [lingering](lingering.md)
through the system login manager. Text displays enabled, disabled, or unknown and
explains that service enablement alone does not establish startup independently of
login. JSON includes a separate `lingering` object with state, attempted status,
account UID, a boolean when known, and successful-observation completion time.
Unavailable observations include an error code and omit the boolean. System-scope
status does not perform or display this account query.

The account query has a separate two-second deadline. Its availability does not
change the service query's exit status or `--check-active` semantics; the dedicated
account diagnostic remains visible. Lingering is not a workload-health check and
status never enables or disables it.

The report separates current record presence, unit/environment file comparison,
pending transactions, manager fragment agreement and live observations. A copied
unit comment does not establish ownership. A missing or unreadable record remains
visible even when systemd can report the unit. A saved record whose unit is missing
also remains inspectable. Corrupt neighboring records make ownership lookup unknown
because they may conceal another claim on the same name.

Live observations share a five-second monotonic deadline, pin the selected manager
identity, and include a completion timestamp in JSON. Available properties include
load/active/substate, enablement, result, main PID, exit code/status, restart count,
fragment path, reload-needed state and transition timestamps. Unknown properties are omitted from JSON
and shown as unknown in text; a known zero is retained. Text escapes terminal
controls. The report does not expose captured argv, environment values, credentials
or the private saved model.

`DropInPaths` reports the manager's ordered list of additional unit configuration
files. Text repeats the label for each path, or shows `none` for a known empty list.
An absent property remains unknown. Status does not read or adopt these files.

A valid ordinary query returns 0 even for failed, inactive or missing installed
services. `--check-active` returns 7 unless the service is observed in `active`
state; this includes active oneshot services whose work has completed. It does not
prove application health. When both saved record and manager unit are absent, the
exit code is 9. Transport, authorization and unsupported-manager errors retain their
specific nonzero status. JSON's `ok` and `exit_code` reflect these same rules.
Storage diagnostics alone do not turn a successful live status query into failure;
they remain explicit in the report.

Disk and manager observations happen at different instants. Fragment agreement is
a lexical comparison with the recorded destination. It does not prove loaded bytes
match disk, interpret drop-in directives, prove lingering/boot guarantees or
authorize a mutation. Those checks and richer installation diagnostics remain
under implementation. Status reads no cached live state and performs no automatic
elevation when private system records are inaccessible.

Validation includes isolated user-layout command fixtures with absent manager
transport, saved-record loading, pure display/exit-state fixtures, and read-only
queries against the development host's real system manager. No host service is
created or changed by this validation.

`NeedDaemonReload` is the manager's boolean observation that the unit's backing
configuration has changed since loading and a configuration reload is recommended.
Text shows `yes`, `no`, or `unknown`; JSON uses a real boolean when known and omits
it when unavailable. The same typed property is included in shared live JSON
observations used by list, recovery inspection, and effective-file inspection.
It is distinct from tired's saved-file digest comparison. A false value does not
prove that the running process uses the latest start context or that the workload
is healthy. Status does not automatically reload or restart anything, and this
observation alone does not change its query exit status.
