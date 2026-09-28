# Encoding contexts

Unit command tokens are always double-quoted. Backslashes and quotes are escaped,
ASCII controls use hexadecimal escapes, and percent specifiers are doubled. The
renderer must prefix ExecStart with `:` to disable environment expansion; this
allows literal dollar signs without depending on the service environment. An empty
argument renders as `""`, and a semicolon remains a quoted argument. This encoding
is for settings that support systemd token unquoting, not arbitrary scalar values.

Scalar directive text has a separate encoder: percent signs are doubled, and
literal quotes/backslashes otherwise remain literal. Controls, boundary spaces,
and a trailing backslash fail because they cannot safely be passed through this
scalar form. Each directive's parser must determine which encoder is appropriate.
Absolute scalar paths use this form only after path validation.

Display encoding is separate again. It preserves UTF-8 and renders ASCII/C1 terminal
controls visibly, doubling literal backslashes to distinguish input from escapes.
Display text is never a replayable command. Comments containing external text must
also use display encoding, while ordinary ownership comments contain only validated
identifiers. Environment files use their own codec described in environment.md.

The output builder grows within a caller-supplied limit, preserves contents on
failed appends, and transfers ownership explicitly. Do not append data that aliases
its allocation. Encoders publish output only after the complete encoding succeeds.

Native tests exercise expansion characters, controls, empty strings, semicolons,
special-prefix arguments, invalid text, and buffer boundaries. The completed unit
renderer still needs real systemd execution round-trip evidence. Syntax details
follow the [baseline systemd syntax documentation](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.syntax.xml).

## Unit serialization

The renderer emits a validated ownership identifier, then Unit, Service, and Install
sections. Within sections it follows the registry's stable field-ID order. Unordered
list values are sorted; argument and external environment-file order is preserved.
The managed environment revision is emitted after all external environment files.
Credentials are references only; no environment or credential values enter unit text.

A proposal must contain its name, absolute executable, working directory, original
argv, type, scope, and resolved enablement target. System scope additionally requires
an explicit run-as user and group. User units omit identity-switching directives.
The executable replaces original argv[0], and ExecStart uses the `:` prefix with
quoted, specifier-escaped tokens. Soft/hard NOFILE fields become one directive.
Resource values use exact numeric forms; durations are emitted in microseconds.

Rendering changes no files or manager state. Failed rendering preserves the previous
output. The complete file is bounded at 4 MiB and each physical line below 1 MiB.
No raw directive/hook insertion is supported. Host capabilities, identities, drop-ins,
path accessibility, ownership, and post-install runtime state still require validation.

The generated native fixture passed systemd 249.11 `systemd-analyze verify --man=no`
on the development host. The verifier also warned about an existing host snapd
unit's unsupported RestartMode key. This is syntax evidence only; actual workload
argument/environment round trips remain part of isolated integration qualification.
