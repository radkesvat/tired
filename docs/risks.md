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

Command sensitivity currently comes from the preview's flag heuristic; it can miss
unknown secret forms. Manual classification, privileged code/dependency inspection,
durable approval records and installation-time enforcement remain to be integrated.
Offline privileged-code checks therefore remain explicitly pending. This inventory
does not imply that a proposal is ready to install.
