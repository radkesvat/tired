# Bounded helper processes

The subprocess API launches an explicit absolute executable with literal argument
and environment vectors. It never searches PATH, composes a shell command or copies
the parent environment. Callers must establish executable trust and choose the
environment. This facility is for explicitly requested helper operations such as
unit verification, never passive workload identification.

Children receive `/dev/null` as stdin, separate stdout/stderr pipes, an empty signal
mask and default signal dispositions. Other descriptors are closed with glibc's
spawn close-from action. CMake checks that this facility exists; it requires glibc
2.34 or newer, available in the intended initial distribution baselines.
Capture still works when parent standard descriptors were originally closed.

Each process has its own process group and a monotonic deadline. `start` checks
input limits and starts capture; `step` performs nonblocking reads and child reaping.
The event loop must call step regularly, normally every 10–25 ms. Each step reads
at most sixteen 4 KiB chunks from each stream, preventing one busy stream from
starving the other or the caller. Maximum duration is 300 seconds, captured output
is limited to 1 MiB per stream, and combined input vectors/path are limited to
1 MiB and 4096 entries per vector.

Timeout, cancellation, output overflow or capture failure closes capture pipes and
sends SIGKILL to the process group. Continue stepping until terminal; destroy only
afterward. Killing is a request to the kernel, not proof that an uninterruptible
process has exited. Cleanup therefore remains nonblocking and may require further
steps. The deadline bounds observation/capture, with detection latency determined
by the caller's stepping interval. Starting through libc can itself wait on kernel
execution setup; this API makes no hard real-time guarantee.

The leader remains unreaped while output pipes are open. This retains its process
group identity if a descendant holds a pipe after the leader exits, allowing safe
group cancellation at the deadline. Other components must not reap these children
or change SIGCHLD handling. Start rejects automatic child-reaping dispositions.
Unexpected external reaping is reported as an I/O error, never a successful exit.
The process group is not a security sandbox; intentionally detached descendants
are outside its cleanup guarantee.

Results distinguish normal exit (including nonzero exit status), signal termination,
timeout, cancellation, output limit and I/O failure. Raw output is borrowed and may
contain NULs or terminal controls. Respect lengths and escape it before display.
No diagnostic text is interpreted as success. A normal process exit does not imply
the requested operation succeeded; consumers must evaluate the exit code.

Native tests exercise both full pipes, binary stderr, literal argv, environment and
descriptor isolation, closed standard descriptors, timeout/cancellation, signaled
exit, failed launch and descendants retaining pipes. The harness reaps its orphan
fixture descendants. Verification staging and UI collection use this bounded
subprocess API. Administrative IPC has its own readiness/version boundary and
independent post-approval worker; its disconnect policy is documented in security
and recovery.
