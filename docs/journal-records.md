# Journal record decoding

The native record decoder reads an already positioned, scoped journal entry. It
does not select entries or advance the journal. The caller must apply the
[journal selection](journal-selection.md) before iteration.

Each record owns its cursor, message, and identifier. Message bytes retain NUL,
invalid UTF-8, and control bytes; they are not safe to print directly. A later
presentation layer must escape them. Missing and present-but-empty messages are
distinct. Messages are limited to 64 KiB and identifiers to 256 bytes, with an
explicit truncation flag. The native data threshold includes the message field
prefix and one additional byte so a message exactly at the bound can be
distinguished from an oversized message. This bounds retained data, not all
internal allocations performed by the journal library.

Realtime timestamps, monotonic timestamps with boot IDs, and nonempty cursors
(at most 4096 bytes) are required. PID, UID, and priority are optional strict decimal values. Missing
values are unknown; malformed or out-of-range values are marked invalid.

Read, authorization, unsupported-format, and allocation failures abort the decode
and preserve the caller's previous record. An absent optional field alone is not
a read failure. The decoder copies each borrowed native field before requesting
another field and clears message and identifier storage when destroying a record.

Tests use an injected native-read boundary that invalidates its buffer on every
read, plus an empty private journal directory to exercise the native adapter.
These checks do not qualify compressed journal files, access discovery, rotation,
follow mode, or the logs command. Those require the journal reader and command
integration.
