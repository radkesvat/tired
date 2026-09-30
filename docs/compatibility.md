# Compatibility

The product targets Linux x86-64 and ARM64, glibc 2.35+ and systemd 249+. Windows,
macOS, musl-only systems and other architectures are unsupported. A container or
WSL instance is usable only when its actual local manager, authorization and
filesystem interfaces support the requested operation; a `systemctl` binary alone
is not enough.

## Build and linkage

First-party code is C17. CMake 3.22+, Clang and LLVM LLD are required; Release must
pass an LTO probe. Direct builds link project/third-party libraries statically and
glibc dynamically, using the oldest intended Ubuntu 22.04/glibc 2.35 baseline.
Debian/PPA builds use distribution-provided shared libraries. Neither mode claims
fully static portability. `tired --build-info --json` and release metadata describe
the mode and declared baseline; [qualification](release-validation.md) records
actual tool/library versions and systems exercised.

The API minima are libsystemd 249, ncursesw 6.2, json-c 0.15 and OpenSSL 3.0. A
baseline build demonstrates the actual installed versions, not every hypothetical
combination at those minima. The declared qualification set is Ubuntu 22.04,
Ubuntu 24.04 and Debian 12 on both supported architectures.

## Native facts and selected features

The local manager's owner, UID, scope, version, unit paths and current properties
are checked. User scope requires a real current-account runtime directory and
manager socket and user broker. Minimal Debian images may need
`dbus-user-session` and `libpam-systemd`, followed by a new login session; packages
recommend these user-scope prerequisites. Missing facilities receive a specific
diagnostic and are never silently created by a service operation. A cold socket-activated user broker may still be registering its
manager; discovery waits within its original deadline for the first verified owner.
Later owner changes invalidate authority and require rediscovery.

Selected resource controllers, credentials, ambient capabilities and descriptor
ceilings must be known compatible. Unknown/unsupported selected features fail
explicitly instead of being clamped. NOFILE must satisfy kernel and selected
manager ceilings; no global or user-manager limit is changed. Supplementary groups
require the manager's actual ability to grant them. Nonroot user filesystem
namespace hardening is rejected on the supported baseline; choose compatible
fields or system scope.

Persistent retries have a nonzero delay and no finite budget. Limited retries can
exhaust their interval/burst. systemd 249 may preserve an earlier `Result=exit-code`
when that budget is exhausted. Status reports the observed result/count; explicit
activation resets only the selected failed unit and cannot fix its application.

## Paths and boot

The command, lexical executable path and invocation working directory are retained.
Resolved identity is captured for validation without rewriting a version-switching
symlink. Unsupported NUL and malformed input fail. External application updates,
including a changed symlink target, invalidate the saved execution identity for
mutating lifecycle operations. Review an explicit `tired edit` to adopt the new
resolved executable identity while keeping the lexical execution path.
Application updates, volatile paths, network dependencies, credentials and live environment files remain
outside tired's control. Doctor can report a missing/unavailable path; it does not
promise repair after boot.

An enabled system service can start at boot independently of tired. User services
also need account lingering for startup without login, plus available home/files.
Network ordering does not establish application health or Internet readiness.
Running/initially completed is a process observation, not an end-to-end health check.

Do not treat unavailable test infrastructure as a pass. The release validation
report distinguishes implementation, actual qualification and owner-controlled
hosting/store/archive acceptance.
