# Persistent unit enablement

`TiredManagerEnablement` calls `EnableUnitFiles` or `DisableUnitFiles` for exactly
one validated full service name. It uses `runtime=false`; enable additionally uses
`force=false`, so it does not authorize replacing existing links. Requests target
the verified unique manager owner with activation and interactive authorization
disabled. A bounded monotonic deadline and cancellation follow the same uncertain
outcome rules as other manager mutations.

The native decoder requires `ba(sss)` for enable and `a(sss)` for disable. It retains
at most 256 changes and 1 MiB of type/path/source text, with per-field limits and
an absolute changed path. Empty change lists and empty source strings are valid.
Unknown change types remain data for diagnostics, never instructions for a generic
filesystem deletion routine. Parsing failure preserves prior decoder output.

Enable replies carry an explicit install-info boolean. Disable replies do not;
the result distinguishes unknown from false. Acknowledgement means a valid method
reply, not that the unit is enabled, disabled, running or healthy. The controller
must query final state and interpret static units and unchanged operations.
Owner invalidation hides the returned change list from current-state consumers.

Before submission the controller must authorize the request, persist intent and
inspect effective install directives, including `Also=` effects beyond the selected
name. Reported change paths do not establish managed ownership or authorize rollback
unlinking. An error may follow partial manager-side changes, so recovery must inspect
actual state rather than assume no mutation. No automatic reconnect or retry occurs.

Native broker tests verify both exact signatures, one-unit arrays, persistent/no-force
flags, pinned owner, successful and empty replies, false install-info, unknown change
types, denial, malformed replies, excessive changes, invalid paths, timeout,
cancellation and owner invalidation. No host enablement changes are performed.
Journal/controller integration and installed-service qualification remain pending.
