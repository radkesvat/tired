# Historical executable identity

The schema-1 executable evidence codec records the last-inspected lexical path,
resolved target path, device and inode. The capture helper copies those values
from an existing passive invocation capture; it does not perform another lookup.
Lexical spelling is preserved, including symlink-sensitive `..` components, and
the resolved target remains separate diagnostic evidence.

Paths are bounded absolute UTF-8 strings without control characters. Numeric
identities use exact unsigned integers and are checked against platform widths.
The codec accepts at most 64 KiB serialized data, rejects missing/extra fields or
wrong types, and replaces owned output only after successful validation.

Reading stored evidence never resolves, opens or executes the referenced file.
It does not establish current accessibility, file contents, identity stability,
interpreter/library trust or authorization for privileged execution. The controller
must associate the lexical path with its saved typed model and re-inspect relevant
facts before committing a change.

Tests cover nonexistent lexical/resolved paths, preserved lexical components,
deep-copy ownership, maximum numeric identities, canonical round trips, invalid
paths/types and output preservation.
