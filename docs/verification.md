# External unit verification

The verification component combines [private staging](verification-staging.md) and
[bounded process capture](subprocesses.md). It invokes the fixed path
`/usr/bin/systemd-analyze` after checking that the executable and each ancestor are
root-owned, not writable by group/other, and not symlinks. The executable must be
regular, executable and non-setid. Root replacement of trusted files is outside
this check's threat boundary.

The literal invocation uses `--system` or `--user`, `--no-pager`, `--man=no`,
`--generators=no`, `verify`, and the full staged filename. These options follow the
[systemd 249 verifier documentation](https://github.com/systemd/systemd/blob/v249/man/systemd-analyze.xml).
The workload itself is never executed. The supplied unit text is staged under its
intended basename, so the candidate takes precedence over an installed unit in
ordinary verifier loading.

The environment contains only a fixed locale, fixed system PATH, disabled colors
and console logging. User scope additionally requires explicit HOME,
XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_RUNTIME_DIR paths for the current identity.
The caller must validate their ownership, existence, identity and intended scope.
No arbitrary environment vector, loader variable, generator path or unit search
override is inherited. Real and effective IDs must match.

Start returns an asynchronous handle; regularly call step until terminal. Cancel
terminates the process group through the subprocess API. Once terminal, the wrapper
attempts staging cleanup and preserves raw stdout/stderr, status, and cleanup error.
Creation failures can also return a terminal handle. Inspect and destroy every
returned handle, reporting a retained staging directory if cleanup failed.

Results deliberately describe only the verifier observation:

| State | Observation |
| --- | --- |
| `CLEAN` | Normal exit zero and neither output stream contains diagnostics |
| `DIAGNOSTICS` | Normal exit zero with output requiring review |
| `NONZERO` | Normal nonzero exit; diagnostics retained |
| `INCOMPLETE` | Launch failure, timeout, signal, cancellation, output overflow or capture error |

Cleanup completion is separate from these states and must also be checked. Even
`CLEAN` is not installation approval, a manager-availability check or an application
health result. Unknown directives can produce warnings without nonzero exit status;
all diagnostics therefore remain unresolved until a consumer classifies them. No
English warning filter silently converts a diagnostic result to clean.

Native integration tests invoke the installed verifier, check valid and invalid
fixtures, detect unknown-option diagnostics, verify cancellation/start-failure
cleanup and confirm that a fixture workload's execution marker is absent. If the
fixed verifier is absent, this integration test explicitly skips.

Live plans and controller preflight use the fixed verifier after manager, identity
and effective-file checks. Diagnostics are presented explicitly; the controller
does not filter English warnings into success. Offline plans use in-memory
validation and do not create stages.
