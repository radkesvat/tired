# Private verification staging

The staging API creates a uniquely named `tired-verify-<UUID>` directory under
`/tmp`, with mode 0700, and a candidate unit with mode 0600. `/tmp` must be a real
root-owned directory; if group/other writable it must have the sticky bit. The
implementation does not honor a caller-controlled temporary-directory environment
variable or overwrite an existing entry.

Callers supply the already normalized service basename and bounded unit text.
Staging appends `.service` exactly once, preserving an existing literal `.service`
within the basename rather than normalizing the identity again. Unit bytes are
limited to 4 MiB and cannot contain NUL. The API writes through the newly created
descriptor, checks partial writes, and synchronizes the file and directory.

The handle retains the parent, directory and file descriptors and their observed
device/inode identities. Keeping the descriptors open prevents inode reuse from
making an unrelated replacement appear to be the original object. Cleanup checks
directory and file identities without following symlinks, removes only those known
entries, and refuses replacements or unexpected directory contents. It does not
recursively remove a tree. These checks do not defend against an already trusted
same-UID process or root concurrently modifying its own files.

Creation failures after directory creation can return a handle for cleanup and
diagnosis. Always call `tired_stage_remove` on a returned handle and check its
result. Report the retained directory if cleanup fails. `tired_stage_destroy`
releases resources only; it must not silently hide failed cleanup by deleting
unexpected files. Successful removal is safe to repeat.

Staging itself neither executes a verifier nor communicates with systemd. Offline
planning does not invoke it. Verification orchestration, scope/drop-in handling,
signal cleanup and interruption recovery remain to be connected before live plans
or installation can use this component.
