# Internal value ownership

`tired_core` is the shared C17 library for the frontend, helper, and native tests.
It has no process-exit or terminal dependencies. First-party warnings apply to the
library and tests independently of third-party build options.

Initialize `TiredText` and `TiredTextList` to zero. They own their allocations and
must not be copied by struct assignment unless ownership is explicitly transferred.
Set and append operations copy borrowed input before changing the destination;
input may reference a current value. A failed operation preserves the destination.
Destroy functions release ownership and reset the object, and may be called again.
An allocated zero-length string is distinct from an unset string. Text preserves
control bytes except NUL; display and serialization layers must encode controls.

List byte budgets include a terminating NUL per item. This bounds collections of
empty arguments as well as nonempty input. Capture uses at most 4096 arguments and
1 MiB including terminators, with additional operating-system limits checked before
installation. Generic value helpers accept caller-supplied tighter budgets.

Integer parsing consumes an explicit byte length and accepts only decimal digits
(with an optional minus for signed integers). It rejects overflow, whitespace,
fractional notation, suffixes, and NUL. Caller bounds are inclusive. Failure leaves
the output untouched. No locale or unchecked libc integer conversion is involved.

Errors carry stable status and code fields, static safe messages, and optional
errno. Input values are not inserted into errors by these primitives. Higher layers
must add field, path, phase, and recovery context without exposing sensitive values.
These primitives do not validate service semantics or encode systemd directives.
