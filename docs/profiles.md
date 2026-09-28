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
