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
