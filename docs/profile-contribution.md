# Profile contribution template

Copy a small existing profile, validate it with `tired profiles validate FILE`,
and supply the following review information. Profiles are data, not executable
setup scripts. Follow [the schema and merge rules](profiles.md).

- **Identity:** stable ID, descriptive name, profile revision and supported modes.
- **Passive match:** exact basename/patterns and their limits; no target probes.
- **Evidence:** primary source URLs, relevant upstream versions, concise summaries
  and the specific directive advice each source supports.
- **Conditions:** system/user scope, actual manager/controller facts, known NOFILE
  ceiling, application mode and any uncertainty. Unknown facts must stay unknown.
- **Changes:** recommendation field, value, rationale, strength and expected unit
  diff. Keep explicit user choices and describe required conflicts.
- **Privilege:** account/capability implications and which choices require explicit
  review. Explain any creation-time `default_run_as` policy explicitly; it must
  preserve account overrides, user scope and existing-service identities. A filename
  does not authorize root or a privileged capability.
- **External inputs:** existing config syntax and passive path references; no reads
  of secret content, executable hooks, command rewriting or app downloads.
- **Fixtures:** generic/explicit/ambiguous matching, overrides, unsupported features,
  negative schema cases and expected generated directives.
- **Update policy:** existing services preserve their saved snapshot until explicit
  refresh and review. Include the advice change in the changelog.

Do not copy large upstream service scripts or documentation into a profile.
Summarize with citations and retain uncertainty when evidence does not establish a
safe general recommendation.
