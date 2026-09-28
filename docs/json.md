# JSON input contract

JSON input is limited to the caller's byte budget and an absolute 1 MiB ceiling.
The scanner allows at most 32 value levels, 16384 values, 1024 keys per object,
and 4096 elements per array. It rejects duplicate decoded keys, including equivalent
Unicode escapes, before constructing authoritative objects with json-c.

The accepted syntax excludes comments, trailing commas, trailing documents, NaN,
infinity, leading-plus numbers, leading-zero integers, and incomplete numbers.
Strings must be UTF-8 with valid Unicode scalar escapes. Lone surrogates and NUL
(including escaped NUL) fail. Integer tokens must fit signed 64-bit negative or
unsigned 64-bit nonnegative ranges. Nonfinite floating results fail. Schema integer
accessors also reject floats and negative integers rather than coercing them.

Returned objects use json-c reference ownership. The caller supplies NULL or an
owned previous reference; success replaces that reference, while failure preserves
it. Successful JSON null is represented by NULL and is distinguished by the boolean
result. Diagnostics do not include input contents.

This layer validates JSON structure, not application schemas. Schema versions,
unknown fields, required fields, cross-references, and semantic constraints must be
checked by their respective loaders. Output serialization uses json-c as well.

The build requires json-c 0.15+ development files and pkg-config. Direct mode selects
a local static archive; distribution mode selects a local shared library and bypasses
CPM. No runtime library sources are copied into the repository. The API baseline is
[json-c 0.15](https://json-c.github.io/json-c/json-c-0.15/doc/html/json__tokener_8h.html).
