# libsystemd dependency preparation

Both dependency modes use the native libsystemd API (249 or newer). Distribution
builds resolve the installed shared library. Direct builds require a static archive
and static capability and journal-compression dependencies. There is
no shared-library fallback in direct mode.

Ubuntu 22.04's libsystemd development package supplies headers and a shared library,
but no static archive. The explicit preparation script builds only `libsystemd.a`
from Debian `252.39-1~deb12u2`, including its distribution patches. This client
reads compact journals used by Debian 12 and Ubuntu 24.04; the manager API
baseline remains 249. It builds against Ubuntu 22.04/glibc 2.35 for direct delivery. It does not install systemd, replace host libraries or build a custom daemon.
Sources and build products remain in an external cache.

Install preparation tools and development libraries:

```sh
sudo apt-get install meson ninja-build clang lld dpkg-dev python3-jinja2 gperf \
  libsystemd-dev libcap-dev liblzma-dev liblz4-dev libzstd-dev \
  libmount-dev libaudit-dev
cmake -DTIRED_ALLOW_DOWNLOAD=ON -P cmake/PrepareSystemd.cmake
```

The download flag is explicit and applies only to this preparation command.
Normal project configure/build never downloads dependencies. For offline preparation,
place the three pinned source-package files in the cache and omit that flag. The
script checks every SHA-256 before extraction and disables Meson wrap downloads.
The source package's `.dsc` signature is not separately verified; integrity is pinned
by these hashes, recorded from the Debian archive:

| File | SHA-256 |
| --- | --- |
| `systemd_252.39-1~deb12u2.dsc` | `f2f952ae61fd40f1ef3ee48c8721e23eaaec5396ef5f1b0c2e138d78fade9c6e` |
| `systemd_252.39.orig.tar.gz` | `08a54a6c4d4cf969fc025eaa8922a55d6bc458100c242510b56c96e7d72af1c5` |
| `systemd_252.39-1~deb12u2.debian.tar.xz` | `27c548c678593cbe82e1701fc480bfe56e99b8f8750cbc09e720b6aa4fcbffaf` |

The source is retrieved from the
[Debian source archive](https://deb.debian.org/debian/pool/main/s/systemd/).
Keep the extracted source and its license files with the cache. Public libsystemd
code is LGPL-2.1-or-later; the source package records additional component licenses.
Direct delivery includes corresponding-source, full-notice and relinking assets.
Security-update review remains a maintainer responsibility. A pinned archive must be rebuilt and the pin reviewed when dependency fixes
are adopted; the pin is not a claim that future security updates are unnecessary.

Default cache location is `$XDG_CACHE_HOME/tired/systemd-252.39-1~deb12u2`, falling
back to `$HOME/.cache/tired/systemd-252.39-1~deb12u2`. Override it with the same
absolute `TIRED_SYSTEMD_CACHE` value for preparation and project configuration.
The archive lives in `build-x86_64-reader/libsystemd.a` or
`build-aarch64-reader/libsystemd.a`. These directories are separate from older
archives built with cryptographic backends. Configuration checks the reader's
generated feature header and rejects archives prepared with OpenSSL or gcrypt.
Preparation is native only. Use separate architecture directories and trusted
source caches; the script does not reset local edits in an existing extracted tree.

Clang builds the archive. The upstream optional C++ probe is disabled; no C++
compiler is required. Upstream generators use their own documented build tools.
Journal XZ, LZ4, Zstandard and audit type-name support are enabled. OpenSSL, gcrypt
and GnuTLS are disabled. tired reads journal data through the public reader API;
it neither creates journal seals nor verifies their authenticity. The reader can
read sealed journals without the sealing implementation. Optional daemon-focused
SELinux, AppArmor and seccomp configuration is disabled for this archive;
the selected libsystemd/basic sources reference these switches only in their build
feature description. No manager daemon or security policy is replaced.

The native library test creates an sd-bus object and links the journal reader without
contacting a manager or opening the host journal. Its post-link check enforces the
direct glibc-only shared-library allowlist, including the transitive dependencies.
Native backend and journal tests exercise live APIs separately; the release
qualification report records the actual hosts and limitations. Distribution builds bypass the source cache and preparation script.

Old direct caches that lack compact-journal support are rejected. Select the current
cache explicitly with `-DTIRED_SYSTEMD_CACHE=...` when reconfiguring an existing
build. Distribution builds continue using their installed shared client.
