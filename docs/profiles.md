# Profile schema and validation

Profiles are declarative JSON documents with schema_version 1. The structural
schema is `schemas/profile.schema.json`; the C loader adds byte limits and typed
semantic validation. A complete validation must use the C loader, not only a generic
JSON Schema validator. The input budget is 256 KiB, with at most 64 basenames,
128 sources/advisories, and 256 recommendations.

Each recommendation names a supported registry field, typed value, applicable scopes,
automatic/suggestion application mode, strength, risk, reason, and nonempty evidence
references. Sources have unique IDs, HTTPS references, valid calendar dates, and an
evidence kind. Every source reference must resolve. Evidence metadata is not proof
that upstream advice has actually been verified; shipped profiles require review.

Identity, command, and working-directory fields cannot be changed by profiles.
Capability recommendations must be suggestions, never automatic grants. Unknown
fields and hooks fail validation. Scalar/list values pass through the same typed
model validator used by CLI assignments. Cross-field/host checks still apply after
recommendations are selected.

Conditions contain one predicate or an all/any collection, with at most eight levels
and sixteen children per collection. Predicates cover scope, service type, exact
argument presence, named host features, and named passive inspections. Features and
inspections have closed vocabularies in the schema. Required recommendations need
conditions. Exact-version profiles carry a version string; version-independent
profiles use null. Unknown version evidence must suppress automatic application.

Basename matching is passive and exact, with optional ASCII case folding controlled
by each profile. A match is a hint, not binary authentication. An empty basename
list is suitable for a generic profile. No executable probing occurs.

The loader owns its parsed document and typed recommendation values; metadata views
remain valid until profile destruction. Failed parses preserve the previous profile.
This layer does not establish trust in profile file locations or apply advice.
The catalog and merger provide the discovery and decision layers described below.
Profile installation, replacement, and installed-service refresh remain unfinished.

## Recommendation evaluation

The merger records one disposition per recommendation: applied, scope mismatch,
false/unknown condition, unknown version, incompatible version, suggestion, user
override, conflicting recommendations, or required conflict. Records align with the
profile's recommendation order, preserving its reasons and source references.

Host facts are explicitly true, false, or unknown. All/any conditions use three-state
logic; missing observations do not become false claims or successful checks. The
caller supplies the manager version or a declared offline target baseline. Exact
application versions must come from trusted passive evidence; absent evidence
suppresses automatic application. No version probe is run.

Type recommendations are resolved before other fields. Remaining type predicates
see that selection, including a user's explicit override. Conflicting eligible
recommendations for the same field retain the input value and are reported together.
Equal recommendations can share the same applied result. Explicit user values remain
unchanged; an incompatible required recommendation gets a distinct warning disposition.

The merger builds a separate owned model, resolves dependent retry/scope defaults,
and performs current semantic checks before publication. Failures preserve previous
outputs. It does not authorize capabilities, certify profile-file trust, validate
host resource ceilings, or implement installed-service refresh by itself. Condition
evaluation and merge feed the offline frontend's provenance views. Explicit-inherit
UI state and full host/risk validation remain unfinished.

## Bundled profile evidence

The bundle contains generic, backhaul, frpc, and frps. Generic adds no application
requirements. Backhaul uses its pinned upstream service example for continuous
restart, a three-second delay, network.target ordering, and descriptor advice.
The NOFILE pair is conditional on `nofile_at_least: 1048576`: unknown or insufficient
usable capacity suppresses both recommendations. The backend must establish that
capacity from host, manager, and service-identity constraints without changing limits.

FRP client/server are distinct exact basenames; `frp` alone matches neither. The
server's network ordering has upstream evidence. Continuous restart for both FRP
components is a tired policy choice, not an upstream requirement. Its source entry
links systemd's behavior reference to explain the policy's mechanism. Existing exec
type and five-second delay remain tired defaults. No FRP high-descriptor or root
requirement is inferred.

Sources were inspected on 2026-09-28. The Backhaul document is pinned to commit
`df7966f8f725837a680ea7b90bd37ea52666c277`. FRP's official setup and systemd pages are
linked directly. These dates establish documentation review, not tested application
version guarantees. No upstream application executable or configuration is bundled.
The profile prose summarizes facts and decisions rather than copying upstream code.

Native bundle tests load all four actual files, check matching/nonmatching names,
retain configuration argv, preserve user choices, suppress system network advice in
user scope, and exercise unknown/insufficient/sufficient descriptor capacity. They do
not execute the target. Offline CLI discovery/application is available after staging
the bundled data; real-host qualification remains separate work.

## Catalog trust and selection

The directory loader accepts only an explicitly supplied absolute directory and
never searches the working directory. It walks ancestors through directory file
descriptors with no symlink following. Every component must be owned by root or the
specified trusted owner and must not be group/other writable. Files must be regular,
have one link, and pass the same owner/write checks. Trusted-root selection is the
caller's responsibility; a bundled label alone is not proof of root ownership.

Files are opened relative to the retained directory descriptor, read within the
profile limit, validated, and hashed with OpenSSL EVP SHA-256. The digest identifies
content, not publisher authenticity. Catalog records retain path, origin, and trusted
owner. Directory order is deterministic; scans stop at 4096 entries and catalogs at
256 profiles. On failure newly appended entries are removed and previous entries
remain. Borrowed record pointers must not survive an append attempt.

Explicit ID selection supports renamed workloads. Automatic matching returns an
ambiguous result when multiple non-generic profiles match, with no arbitrary winner.
Generic is used only as fallback. User-origin entries are excluded in system scope.
Duplicate IDs currently fail closed; the explicit replacement installation workflow
is not yet implemented. Frontend discovery uses the configured installation data
directory, administrator directory, and user configuration directory in user scope.
Offline output lists ambiguity candidates instead of selecting a winner.

## Read-only profile commands and plans

`profiles list` displays available IDs and names; JSON includes source paths, origins,
and content digests. `profiles show ID` displays the selected document. `profiles
validate FILE` validates an explicitly selected file without a manager or catalog
trust claim. The current implementation does not install, remove, or refresh profiles.

Offline planning defaults to auto matching after generic capture and explicit
selection. `--profile none` skips discovery. A selected profile is independently
snapshotted into the plan, so its reasons and source metadata survive catalog cleanup.
JSON output includes the snapshot, digest, source, origin, and each recommendation's
disposition. Text output explains recommendations and cites evidence. Changing profile
selection requires rebuilding the input proposal to avoid retaining stale advice.

Offline evaluation declares systemd 249 as the target baseline; this is not an observed
host version. Features, application versions, and usable NOFILE ceilings remain unknown
unless independently supplied by a validated context. Conditional advice stays visible
but is suppressed when its evidence is unknown. No manager queries or target probing
are performed by this path.

Development binaries use their configured install prefix for bundled data. Run
`cmake --install build/<preset>` to stage the executable and profiles. No source-tree
or current-directory fallback is compiled into production. `profiles validate FILE`
and `plan --profile none` work without installed bundled data.

`tired profiles explain [options] -- COMMAND [ARG...]` evaluates a captured invocation
without executing it or writing files. The explicit `--` is required; everything
after it belongs to the workload, including strings that resemble frontend flags.
Creation options such as `--profile ID`, `--user`, `--restart`, and `--set` let you
inspect how explicit selections affect advice. `--json` emits the evaluated proposal
with `command: "profiles explain"`, match candidates, retained profile evidence and
recommendation dispositions. Text output includes the match basis, source, digest,
evidence and suppressed advice. Both use ordinary plan redaction; unknown secret
forms may evade that heuristic. Output-file and sensitive-export options are not
accepted for this command.

Explanation uses the declared systemd 249 baseline and leaves unobserved host facts
unknown. It does not establish live compatibility or installation readiness. Use
`profiles show ID` to inspect a profile document without a workload. Explain with
`--profile none` bypasses catalog discovery and shows the generic configured proposal.

Plans with matching enabled and `profiles list/show` also load additional directories
from administrator [settings](settings.md). Those directories must exist and pass
root ownership and permission checks; they do not weaken duplicate-ID rejection or
grant user profiles system trust. Discovery preserves the prior catalog if any
location fails. Standalone file validation does not load installed settings.
