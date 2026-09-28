# Typed service fields

The field registry associates a stable field ID and public name with its type,
canonical choices, default, and generated directive. Internal code queries the
registry instead of using arbitrary unit directives. Unknown fields are rejected;
there is no raw directive or hook field.

The current scalar model contains owned UTF-8 text, enum choices, booleans, signed
bounded integers, and exact microsecond durations. Values record whether they are
unset, inherited, defaulted, profile-derived, user-selected, or captured. Explicit
false, zero, and empty text are different from inherited or unset fields. Text
ownership and failure behavior follow the internal value APIs.

Duration inputs use a decimal number with up to six fractional digits and an
optional `us`, `ms`, `s`, `min`, `h`, or `d` suffix. No suffix means seconds.
Fractions must be exactly representable in microseconds; values are never rounded.
Whitespace, signs, scientific notation, compound durations, and overflow fail.
Infinity support for applicable resource/timeout fields is not yet implemented.

Scalar assignment validates syntax without claiming the host can apply a setting.
Callers must perform semantic, risk, identity, filesystem, manager, rendering, and
commit-time validation afterward. A proposal under construction may lack captured
fields. `tired_spec_validate_scalars` checks only implemented scalar combinations;
it is not permission to install an incomplete model.

Default construction replaces the destination atomically. Field assignment copies
text and preserves the previous field on failure. Duplicate user scalar assignments
are errors unless the caller explicitly selects replacement for an editor operation.
Profile merge precedence must be enforced by the profile layer rather than treating
assignment as a general authorization or merge operation.

The registry currently covers basic identity/execution, restart controls, timeouts,
output modes, scalar hardening, scope/network, and requested activation. Collection
fields, resource-limit types, signal validation, full provenance, and the CLI/TUI
consumers remain under implementation.
