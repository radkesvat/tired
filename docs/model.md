# Typed service fields

The field registry associates a stable field ID and public name with its type,
canonical choices, default, and generated directive. Internal code queries the
registry instead of using arbitrary unit directives. Unknown fields are rejected;
there is no raw directive or hook field.

The current scalar model contains owned UTF-8 text, enum choices, booleans, signed
bounded integers, and exact microsecond durations. Values record whether they are
unset, inherited, defaulted, administrator-configured, user-configured,
profile-derived, user-selected, or captured. Explicit
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
output modes, scalar hardening, scope/network, requested activation, and the
collections, resource types, and process controls listed below. Full provenance and the CLI/TUI
consumers remain under implementation.

## Collections and retry defaults

Ordered lists now cover arguments, supplementary groups, external environment files,
unit dependencies, mount/write paths, and runtime/state directories. Each item is
copied without whitespace splitting. Argument lists allow 4096 items; other lists
allow 1024. Each list has a 1 MiB budget including terminators. Empty arguments are
preserved; clearing a list records an explicit empty collection. Mixed-origin
appends require the caller to resolve precedence first, so provenance is not silently
reassigned. Per-item provenance and profile merging remain to be implemented.

Environment/mount/write paths must be absolute. Runtime/state directories use
relative ASCII components and reject empty, dot, and parent components. Dependency
names require a recognized unit-type suffix and conservative ASCII characters;
existence and activation effects require manager validation. Escaped unit names
and directory creation modifiers are not currently accepted by this parser.

After input assignment, retry-default resolution chooses zero start-limit interval
for persistent mode, or five minutes and ten starts for limited mode. It updates
only inherited/default fields. Conflicting explicit/profile intervals fail without
changing the model. Persistent retries reject a zero delay. A subsecond-delay risk
acknowledgment belongs to the pending risk-validation layer.

## Resource values

`nofile.soft` and `nofile.hard` are a required pair when either is supplied; the
soft value cannot exceed the hard value. Each is an unsigned decimal quantity or
`infinity`, represented separately from numeric values. Numeric UINT64_MAX is
rejected instead of silently becoming an infinity sentinel. No descriptor limit
is applied by default.

`memory_max` accepts integer bytes, binary K/M/G/T/P/E multipliers, or `infinity`.
`tasks_max` accepts a positive integer or `infinity`. `cpu_quota` accepts a positive
percentage with at most two decimal places and preserves values over 100%.
`umask` accepts up to four octal digits bounded by 0777; runtime/state directory
modes accept up to 07777. These values remain inherited unless selected.

Parsing performs no clamping or global limit changes. Host controller availability,
manager limits, kernel ceilings, and field-specific risk disclosure must still be
validated before installation. See the baseline
[resource-control reference](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.resource-control.xml)
and [execution reference](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.exec.xml).

## Signals, capabilities, and enablement

`kill_signal` defaults to SIGTERM. The parser accepts standard Linux signal names
with an optional SIG prefix, valid decimal signal numbers, and RTMIN+N/RTMAX-N
forms. Zero, libc-reserved signal numbers, and out-of-range realtime offsets fail.
The stored value is a signal number. Exit-status lists accept exit codes 0–255 or
signal names; numeric entries in those lists mean exit codes, not signals.

Capability lists accept canonical uppercase CAP_* names through
CAP_CHECKPOINT_RESTORE. Unknown names and set-inversion syntax fail. If an explicit
bounding set exists, each ambient capability must belong to it. The default remains
inherited, with no new capabilities requested. A valid name is not evidence that
the running kernel or service identity can provide it; granting capabilities still
requires explicit user intent, risk disclosure, and host validation.

Scope resolution selects multi-user.target for system services and default.target
for user services. Explicit conflicting targets fail instead of being rewritten.
Only these two enablement targets are currently supported. Ordinary dependency
lists remain separate from installation enablement.

The stop and exit-status semantics are based on the baseline
[systemd.kill reference](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.kill.xml)
and [systemd.service reference](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.service.xml).

## Explicit inheritance

An optional field can carry `origin=user` and `inherit=true` with no stored value.
This is distinct from an unspecified/inherited default, explicit false or zero, and
an explicit empty collection. It survives copying and profile merging as a user
choice. The renderer omits its directive, and JSON reports the inheritance flag.

`tired_spec_inherit` releases any owned value and records that state. Required
identity/execution fields and product policy selectors cannot be inherited. Semantic
constraints still apply: persistent retries require a known nonzero delay and a
zero start-limit interval; NOFILE must specify both bounds or inherit both. Resetting
an entire model to defaults is a separate operation that intentionally discards edits.

The CLI exposes `--unset FIELD`. Assigning and unsetting the same field in one
request is an error, including collection and external-environment-file options.
The future editor uses the same model operation when the user chooses inheritance.
