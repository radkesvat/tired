# Service logs

`tired logs NAME` reads local default-namespace journal records selected by the
full service name and manager scope. It does not require a saved tired record or
load/start/reload the service. This also permits inspection of historical logs
after a unit has disappeared. System scope is the default; `--user` selects the
calling user's manager and UID. No automatic elevation occurs.

Examples:

```console
tired logs relay --lines 50 --boot current
tired logs relay --user --follow
tired logs relay --json --since 2026-09-29T12:30:00Z
tired logs relay --lines 0 --follow
```

The default tail size comes from effective `log_tail` settings (200 unless
configured). `--lines` accepts 0 through 10000. `--since` filters the selected tail
and future entries; its UTC and epoch forms are described in
[log options](log-options.md). Explicit boot IDs or `current` constrain every
match. Follow prints new entries until SIGINT, SIGTERM, or SIGHUP cancels the
command, returning 130. Signal handlers and output descriptor flags are restored.
Output waits remain cancellable even if a pipe consumer stops reading. Native
journal and filesystem calls do not have a hard IO deadline.

Plain text uses escaped ASCII with timestamps. JSON is newline-delimited and
includes `journal_access`, `journal_record`, `journal_invalidate`, and
`journal_end` events. Every event identifies the selected service and scope;
user events also identify the selected UID. Execution errors use `journal_error`;
argument-parsing errors use the general command error format. A write failure may
leave a partial final event, so it is reported on stderr instead of appending JSON
to damaged output. See [record output](journal-output.md) for binary fields.

Access is checked before reading, after file-set invalidation, and before a normal
exit. Limited access is reported and leaves exit status 8 even if later checks
succeed; available records are still displayed. Successful reads return 0, native
or output errors return their error status, and cancellation returns 130. Missing
storage roots have their own count. Zero displayed records means zero matching
records observed in accessible files, not proof that the service never logged.
Native libraries can silently drop damaged journal content; access checks only
cover enumeration and file opening. Events explicitly state that content
completeness is not guaranteed. Rotation can remove history, and sequential follow
does not replay entries newly inserted before its current position.

Application logs may contain secrets. No automatic secret-redaction promise
applies. The command writes only to the caller's output stream; it does not create
files or transmit logs. `--output` export is unsupported. Explicit shell redirection
is controlled by the user and its permissions are determined by the shell.
The command never changes journald storage or retention.

Private native-directory integration tests cover empty and malformed journals,
structured diagnostics, output failure, follow cancellation, and cancellation with
a full output pipe. Record/selection/stream fixtures separately cover binary data,
UID isolation, tail order, and append behavior. Populated compressed journals,
real rotation, ARM64, and the full platform matrix still need qualification.
