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
Discovery, trust checks, ambiguity handling, merge provenance, bundled evidence,
profile commands, and profile refresh remain under implementation.

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
evaluation and merge are now implemented; discovery, per-file trust/digests, ambiguity
across profiles, explicit-inherit UI state, and end-user provenance views remain.
