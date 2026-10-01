# Dependencies and notices

First-party tired code is MIT licensed, copyright 2026 radkesvat. The pinned CPM
build helper retains its own MIT notice under `cmake/vendor`. No application binary
is bundled merely because a declarative profile is included.

| Library | Purpose | Library license selected for direct delivery |
|---|---|---|
| libsystemd | Native manager/logind/journal APIs | LGPL-2.1-or-later |
| ncursesw and tinfo | Wide terminal UI | MIT/X11 notices |
| json-c | Strict bounded JSON representation | MIT |
| Nettle | SHA-256 fingerprints | LGPL-3.0-or-later option |
| libcap | libsystemd capability support | BSD-3-Clause option |
| liblzma | libsystemd compression | Public-domain library portions; package notices retained |
| lz4 | libsystemd compression | BSD-2-Clause library portions |
| zstd | libsystemd compression | BSD-3-Clause option |
| Compiler runtime support | Static compiler support selected by the recorded Clang toolchain | GCC runtime exception with full notices |
| glibc | Platform C runtime and explicit memory clearing, dynamically linked | Distribution-managed LGPL library |

Source packages can include utilities/documentation under additional licenses.
The full upstream/distribution copyright notices, rather than this short table,
identify the applicable files. Build and release inventories record actual versions
and linkage; API minima are documented in [compatibility](compatibility.md).

## Linking and source delivery

Direct artifacts statically link non-glibc libraries. Debian/PPA artifacts use
installed shared libraries and derive runtime package dependencies. Both modes
keep glibc dynamic. Distribution mode bypasses CPM and includes no private runtime
library copies. Static libsystemd preparation and its verified source pin are
explained in [systemd dependency preparation](systemd-dependency.md).

For direct releases, provide full notices/license texts, the exact corresponding
LGPL library source packages and preparation instructions, and object/archive
material that permits relinking tired with modified libraries. The companion
relink archive contains first-party main/helper objects, the core archive, link
arguments, exact compiler/toolchain information and a CMake relink recipe. Matching
Clang/LLD is needed when Release objects contain LTO bitcode. The upstream tired
source is also supplied under MIT so first-party objects can be rebuilt.

The corresponding-source bundle must include the exact source/debian patches used
for libsystemd and Nettle; include the other static dependency
sources/notices as well. The bundle includes [source build/relink instructions](dependency-source-build.md). Keep these generated release materials outside source
history. Verify replacements by relinking both frontend and helper and inspecting
ELF linkage, without installing or executing a privileged helper. Publish these
companion assets alongside direct binaries, not as an inaccessible build cache.

See the [LGPL 2.1 text](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html),
[LGPL 3 text](https://www.gnu.org/licenses/lgpl-3.0.html), and
the preserved full notices in the release. This document describes the actual
prepared delivery rather than substituting a license inventory for its required
source/relink artifacts.

SHA-256 is provided by Nettle through a shared first-party wrapper for one-shot and
streaming input. Existing lowercase hexadecimal digests and stored fingerprints
remain unchanged. Sensitive buffers use glibc `explicit_bzero`; random identifiers
use the kernel's `getrandom` interface. No project code implements cryptographic
primitives or uses OpenSSL or libgcrypt APIs.

The direct libsystemd reader disables cryptographic backends while retaining all
supported journal compression formats. It reads journal entries, including entries
in sealed journals, but does not authenticate their seals. Distribution mode uses
the distribution's libsystemd and its package-managed transitive dependencies.
The `openssl` executable is used only by the installer test's local HTTPS fixture.
