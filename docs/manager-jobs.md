# Tracking native systemd jobs

`TiredManagerJob` supports `StartUnit`, `StopUnit` and `RestartUnit` for a selected
safe full service name with job mode `replace`. It first installs and acknowledges
a `JobRemoved` signal match, then calls `Manager.Subscribe`, and only after that
submits the requested job. Calls target the verified unique manager owner with
activation and interactive authorization disabled. AlreadySubscribed is accepted
for a shared connection; the operation does not unsubscribe other consumers when
it ends. Manager subscription lifetime is bounded by the connection.

One monotonic deadline covers match installation, subscription, submission, reply
and completion. The method reply supplies a canonical job path/ID. Up to 32 early
completion signals for the selected unit are retained while that ID is unknown.
The exact returned job is selected; unrelated units and other jobs for the same
unit cannot satisfy completion. Overflow fails explicitly rather than dropping
possibly relevant evidence. Baseline sd-bus stops argument matching at a numeric
argument, so JobRemoved's unit is filtered in the callback, not with an arg2 rule.

Results distinguish queued submission, accepted job ID, completion signal and
error. A non-`done` outcome preserves its typed completion evidence but reports
unsuccessful execution. Success still requires later runtime observation to prove
service health. Manager-owner invalidation prevents reuse of a successful result
against a replacement manager while retaining the earlier completion evidence.

Cancellation stops waiting and releases local match/call slots. It does not call
CancelJob or undo submitted work. Timeout, transport loss and missing replies may
leave the actual job outcome unknown. The controller must persist intent before
starting, verify manager version and authorization, and reconcile uncertainty.

Native broker fixtures verify subscription ordering, pinned destination, flags,
literal unit/mode arguments, early and late signals, failed outcomes, denied calls,
timeouts, cancellation before and after submission, unrelated events, bounded
early-event overflow and later owner invalidation. They submit no host jobs.
Controller/journal integration and installed-service qualification remain pending.
