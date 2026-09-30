# Proposal risks

The shared risk registry defines these stable codes:

| Code | Trigger |
| --- | --- |
| `run-as-root` | Root service identity selected from a nonroot invocation |
| `privileged-capabilities` | A nonempty ambient capability set |
| `writable-privileged-code` | Root or capability-bearing execution with observed less-trusted writable code or parents |
| `rapid-persistent-retry` | Persistent retries with a delay below one second |
| `sensitive-command-data` | Classified sensitive data retained in command metadata |
| `restore-drifted-unit` | Explicit replacement of foreign edits |
| `sensitive-export` | Explicit export of unredacted data |
| `leave-child-processes` | KillMode process/none can leave workload processes alive after stopping/removal |

Assessment combines a validated service model with facts supplied by the caller.
It performs no filesystem inspection, manager access or approval. Privileged
execution with uninspected code records a pending check instead of assuming the
code is safe or writable. A capability bounding set alone does not grant ambient
capabilities. Existing root-origin flows do not count as newly selecting root.

The acknowledgment checker validates every code, refuses pending checks and requires
each present risk to be acknowledged individually. It has no `--yes` bypass.
Passing it proves only that this supplied risk inventory has acknowledgments; it
does not authorize installation or replace identity, filesystem, syntax, protocol,
concurrency or manager validation. Privileged consumers must establish facts
independently and must not trust frontend assertions or profiles as approvals.

Offline plans and profile explanations include `risk_checks` entries with a code,
message and `inspection-pending` or `acknowledgment-required` state. Identified risks
also appear in the existing JSON warnings list and full text previews. The state
describes the requirement, not a saved approval. These read-only commands do not
require acknowledgments merely to inspect a proposal. Unknown `--allow-risk` codes
are rejected by the CLI. Sensitive exports retain their separate private-file and
explicit-acknowledgment checks.

Command sensitivity comes from explicit `--sensitive-arg INDEX` classifications and
the preview's flag heuristic; the heuristic can miss unknown secret forms.
Privileged execution checks executable/resolved ancestry and the working directory;
relative scripts/modules can depend on that directory. Saved review snapshots retain
classifications and acknowledgments. Changed high-risk choices clear their previous
acknowledgment. The helper independently revalidates facts under lock. This does not
prove an entire interpreter/module/config dependency graph. Offline checks remain
explicitly pending until native validation occurs.
