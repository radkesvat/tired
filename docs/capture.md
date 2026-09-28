# Captured command context

Command capture is passive. It copies each argument independently, including empty
arguments and literal shell metacharacters, and records the actual current working
directory and real UID/GID before elevation. It does not execute the target, import
the process environment, interpret shell syntax, or change directory to the binary.

Commands containing `/` are made absolute relative to the captured directory.
Other commands use the supplied invoking PATH once, including empty and relative
PATH entries. An absent PATH requires an explicit path. The selected file must be
regular and executable by the invoking identity. This is an initial check; it does
not prove executability under the eventual service identity or validate a script's
interpreter. Installation must revalidate those conditions.

The execution path retains symlink components and `..` exactly. Canonicalizing them
could change the command or freeze a version-switching symlink. Capture separately
records the current canonical target and device/inode for diagnostics, rejecting
an observed target change during capture. These observations are not locks or an
authorization mechanism; commit-time revalidation remains necessary.

Original argv is stored unchanged. The unit renderer will use the absolute lexical
execution path for argv[0], preserving every following argument. Capture rejects
NUL and malformed UTF-8. Paths additionally reject ASCII C0/DEL controls; arguments
retain controls for context-specific encoding. Spaces, quotes, and valid Unicode
are accepted. Paths and PATH input are bounded by 1 MiB; arguments are bounded by
4096 entries and 1 MiB including terminators. Tighter OS limits are checked later.

Shell aliases, functions, shell history, unexported variables, inherited file
descriptors, terminal state, namespaces, and SSH-agent access are not reconstructed.
Explicit shell workloads remain visible commands, not profile-generated wrappers.
