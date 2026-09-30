# Design decisions

## Ordinary systemd services are the runtime

The control program captures and reviews a typed command, then creates an ordinary
unit. It never adds a wrapper supervisor or resident dependency. Installation,
enablement, current process state and application health remain separate facts.
This makes reboot and package removal behavior inspectable through native tools.

## Authority and execution identity are separate

System installation uses a fixed ordinary helper through existing sudo policy.
The helper verifies the invoking account, request, manager, files and acknowledgments
again under the scope lock. The workload retains its selected nonroot identity.
There is no installed authorization policy, setuid helper or capability shortcut.
After approved transfer, one bounded independent worker owns completion; terminal
or SSH loss cannot be treated as cancellation of an already admitted operation.

## Profiles advise without executing

Profiles are bounded declarative local data. Matching and recommendations retain
source, evidence, trust and uncertainty. Filename matches cannot grant privilege.
Existing records contain a profile snapshot; changing installed profiles cannot
silently change running services. Explicit refresh clears acknowledgments for newly
introduced high-risk choices and requires review.

## Durability precedes observation

Private immutable revisions, exact fingerprints and intent/completion ledgers bind
each external step. Recovery observes uncertain admitted manager actions instead
of replaying a restart. Rollback restores control-plane intent and reports actual
runtime restoration; it cannot undo application side effects. Files changed by
other administrators remain conflicts requiring explicit review.

## Conservative baseline compatibility

The complete product targets systemd 249 and glibc 2.35. Selected resource limits,
groups, priorities and namespace features use actual manager facts; unknown facts
produce explicit incompatibility. Nonroot user filesystem namespaces are refused
on the supported baseline. Account lingering is explicit current-operation intent,
separate from its historical record, so ordinary stop/remove does not replay it.

## Separate distribution delivery modes

Direct builds statically link non-glibc dependencies and carry notices, exact
corresponding sources and relinking material. Debian/PPA builds use shared distro
libraries and generated runtime dependencies. Both retain dynamic glibc. Snap is a
complete classic package because arbitrary host units, journals, identities, sudo
and workload paths exceed available strict interfaces. External classic review
and archive/store publication remain maintainer actions.
