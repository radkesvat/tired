# Journal record output

The journal renderer produces one newline-terminated event per decoded record.
It does not open journals, transmit logs, or save files. Application log content
is not automatically secret-free; no secret redaction is implied.

JSON events have `schema_version: 1`, `event_type: "journal_record"`,
`selected_service`, and `scope`. User scope additionally has `selected_uid`, the
UID of the selected manager. This differs from the optional emitting process
`uid`. Cursor, boot ID, realtime microseconds, and monotonic microseconds preserve
native record identity and timestamps. Consumers must support unsigned 64-bit
integers to preserve timestamp precision.

`message` and `identifier` contain a `present` boolean. Present fields also carry
`encoding: "hex"`, lowercase hex `data`, original retained byte `length`, and
`truncated`. An empty present field has empty data; an absent field has no data.
This representation preserves NUL and invalid UTF-8 without producing invalid
JSON. PID, UID, and priority objects distinguish known values from missing or
invalid values; unknown values have no numeric `value`.

Plain text uses Unix realtime seconds with six fractional digits, selected scope
and service, optional identifier, and message. Non-ASCII and control bytes are
rendered as `\xNN`; literal backslashes are doubled. This ASCII rendering never
lets a message insert extra terminal lines or control sequences. It shows explicit
markers for missing messages and truncated fields. Structured consumers should use
JSON rather than interpreting text markers as metadata.

Output is bounded to 512 KiB per record, including the worst-case expansion of a
64 KiB message. Failed rendering preserves the previous caller-owned output.
This component is ready for command integration; the logs command and journal
access diagnostics are still separate work.
