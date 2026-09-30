# Security and ownership

Installing a system unit requires administrator authority; executing its workload
as root is a separate decision. System scope defaults to the captured account.
Validated sudo metadata may identify the original account, but cannot authorize an
operation. User scope always uses the current UID/GID and its verified existing
manager. Discovery does not execute a binary, interpreter, config hook or profile.

## Administrative boundary

The helper is an ordinary executable, without setuid bits or file capabilities.
No permissive sudo or polkit policy is installed. The frontend invokes a fixed,
trusted helper through ordinary authorization. Headless authorization uses sudo's
noninteractive mode and fails promptly if credentials are unavailable. The helper
verifies its own UID, the authorized actor, protocol version, digest, operation,
accounts, current files, risks and manager identity under the scope lock.

The root helper accepts neither arbitrary paths nor alternate layout roots from
IPC. It sanitizes the environment, closes unintended descriptors and uses trusted
administrative tools. Payload lookup honors the installation prefix while checking
both original and resolved helper ancestry; an attacker-owned install tree cannot
become a privileged helper through a symlink.

## Filesystem and race protections

Mutation destinations derive from the selected scope, safe unit names and canonical
UUIDs. Trusted directory descriptors, no-follow opens, regular-file/owner/mode/link
checks, exact fingerprints and checked publication protect against symlink,
hardlink and concurrent replacement attacks. Existing vendor units, aliases, masks,
dangling links and drop-ins are conflicts, not permission to overwrite.

System units are readable administrative configuration; command-line secrets can
therefore remain visible there and in process metadata. Private service records,
transaction journals, environment revisions and history use restricted permissions.
The program verifies private environment hashes and references before execution or
editing. Hashes are integrity evidence, not encryption or protection against root.

## Secrets

Only explicit `--env`, `--pass-env`, snapshot imports and credential references are
captured. No whole-shell environment is copied. Recognized secret names/flags and
`--sensitive-arg INDEX` are redacted in ordinary output. Heuristics cannot discover
every secret; use explicit classification and avoid secret command arguments.
Terminal controls in paths, profile data and journal text are displayed as escaped
data. Journal messages can contain application secrets that tired cannot classify.

`plan` and `show` can write a new private `--output FILE`. Unmasked output also
requires `--include-sensitive --allow-risk sensitive-export`. Existing files and
symlinks are refused. Generated environment assignments live in immutable private
revisions; records describe names, origin and classification without duplicating
plaintext values. Privileged administrators, crash dumps and filesystem backups
may still expose private data.

`--credential NAME=PATH` emits `LoadCredential` for applications that already
support systemd credentials. It neither reads a secret during discovery nor
rewrites application flags or config. Managers load external environment and
credential files under their own authority; tired checks their current accessibility.

## Resource and execution constraints

Selected descriptor limits, controllers, ambient capabilities and supported
hardening are validated against native facts. Unsupported or unknown selected
features are refused rather than clamped. Root/capability execution checks code and
parent-directory ownership/writability. This cannot prove an interpreter's entire
module/config dependency graph or freeze independently updated application files.
Nonroot user filesystem-namespace hardening is unavailable on the supported baseline
and is rejected explicitly. Global limits, user-manager limits and wait-online
services are never changed automatically.

`--hardening baseline`, or `H` followed by Space in review, explicitly selects
`NoNewPrivileges=true`, `PrivateTmp=true` and `ProtectSystem=full`. This blocks
privilege gains, gives the workload private temporary directories, and makes
`/usr`, `/boot` and `/etc` read-only. These restrictions can break an application;
review the exact unit and its writable paths before approval. The same native
availability checks apply to presets and individual fields. The preset requires
mount namespaces and is unavailable for nonroot user services on the supported
baseline. It is never part of an ordinary default proposal.

Inputs, JSON nesting, argument/environment counts and bytes, tool output, journal
buffers and history are bounded. Literal command vectors are used throughout;
there is no shell command construction, raw unit directive escape hatch or
executable profile hook. Review all [risk codes](risks.md).

Report vulnerabilities according to [SECURITY.md](../SECURITY.md). The technical
checks protect managed state; they do not guarantee application correctness or
restore application side effects after a rollback.
