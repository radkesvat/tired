# Typed unit observations

The observation decoder consumes an unread `a{sv}` body from a Properties.GetAll
reply for the Unit or Service interface. It does not issue a request, verify the
sender or claim that a unit is managed by tired. The backend must establish those
properties before passing a reply to it.

Every field has a separate `known` flag. A missing MainPID differs from a reported
zero PID; an absent exit status differs from zero; an absent fragment path differs
from a reported empty path. Unknown states remain unknown rather than receiving
default values that could imply success.

| Interface | Fields and wire types |
| --- | --- |
| Unit | Id, LoadState, ActiveState, SubState, UnitFileState, FragmentPath: strings |
| Unit | ActiveEnter/ExitTimestamp and InactiveEnter/ExitTimestamp, including each Monotonic counterpart: unsigned 64-bit |
| Service | Result: string; MainPID and NRestarts: unsigned 32-bit |
| Service | ExecMainCode and ExecMainStatus: signed 32-bit |
| Service | ExecMainStart/ExitTimestamp and Monotonic counterparts: unsigned 64-bit |

These types follow the baseline manager interfaces. Timestamps retain native
microseconds and their wall-clock/monotonic distinction. State strings are retained
without assuming that the vocabulary will never grow.

Decoding replaces the selected interface's fields as a group, clearing properties
absent from the new response. It preserves the other interface's existing values.
Failure preserves the complete previous observation. This atomic memory update
does not imply that separately queried Unit and Service properties describe one
atomic instant in the manager.

The decoder accepts at most 512 dictionary entries and 128 KiB of property names.
Names must be valid D-Bus member names and unique, including unknown properties.
Known fields require their exact wire type and correct interface. Strings are
bounded to 256 bytes, except FragmentPath at 4096, and reject invalid UTF-8/control
text. Unknown properties are skipped for forward compatibility; their wire decoding
remains subject to libsystemd's D-Bus message limits. Owned destinations start zeroed
and must be destroyed after use.

Native message fixtures exercise known-zero versus absent values, exact integer
width/sign, large timestamps, unknown variants, duplicate keys, type errors,
property-count limits, preservation on failure and clearing stale fields. Actual
GetAll request orchestration and status/doctor/frontend presentation remain pending.
