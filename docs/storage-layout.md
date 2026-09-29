# Storage locations

The layout resolver derives mutable-state paths independently of the executable's
installation prefix. System scope always uses these locations:

| Data | Location |
| --- | --- |
| Configuration | `/etc/tired/config.json` |
| Administrator profiles | `/etc/tired/profiles.d` |
| Revision environment service directories | `/etc/tired/services` |
| Unit files | `/etc/systemd/system` |
| Current service records | `/var/lib/tired/services` |
| Prior control-plane revisions | `/var/lib/tired/history` |
| Recovery transactions | `/var/lib/tired/transactions` |
| Mutation lock | `/run/tired/operation.lock` |

User scope puts configuration, profiles and environment service directories under
`$XDG_CONFIG_HOME/tired`, units under `$XDG_CONFIG_HOME/systemd/user`, service
records/history/transactions under `$XDG_STATE_HOME/tired`, and the lock at
`$XDG_RUNTIME_DIR/tired/operation.lock`. Unset or empty config/state variables
default to the account database home plus `.config` and `.local/state`. `HOME`
does not override the account database. Runtime has no guessed fallback.

System discovery never reads user environment paths. User discovery requires
matching real/effective user and group IDs. The pure resolver can also receive
explicit user path inputs for controlled callers and tests; these inputs cannot
override system destinations.

Paths must be absolute, have no dot/traversal components or repeated interior
separators, and contain valid text without control characters. Trailing separators
are removed. Components are bounded to 255 bytes and complete paths to 4096 bytes,
including appended suffixes. Failed resolution preserves the prior layout.

Before creating a user unit, the controller must check that the resolved unit
directory occurs in the selected manager's UnitPath. Live name discovery enforces
this check using the scope retained by manager identity discovery. The check compares lexical
paths with trailing separators ignored; it refuses mismatches rather than guessing
that a service will be visible. It does not resolve filesystem aliases.

These APIs only derive and compare paths. They create nothing and do not prove
ownership, accessibility or authority to write. Storage operations must separately
validate directory ownership and permissions, reject unsafe links, use private
state directories and files, and perform mutations through validated descriptors.
The transaction store, record schema and lock operations are separate components.
