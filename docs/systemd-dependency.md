# libsystemd dependency preparation

Both dependency modes use the native libsystemd API (249 or newer). Distribution
builds resolve the installed shared library. Direct builds require a static archive
and static capability, cryptographic and journal-compression dependencies. There is
no shared-library fallback in direct mode.

Ubuntu 22.04's libsystemd development package supplies headers and a shared library,
but no static archive. The explicit preparation script builds only `libsystemd.a`
from the Ubuntu `249.11-0ubuntu3.22` source package, including its distribution
patches. It does not install systemd, replace host libraries or build a custom daemon.
Sources and build products remain in an external cache.

Install preparation tools and development libraries:

```sh
sudo apt-get install meson ninja-build clang lld dpkg-dev python3-jinja2 gperf \
  libsystemd-dev libcap-dev libgcrypt20-dev liblzma-dev liblz4-dev libzstd-dev \
  libmount-dev libaudit-dev libssl-dev
cmake -DTIRED_ALLOW_DOWNLOAD=ON -P cmake/PrepareSystemd.cmake
```

The download flag is explicit and applies only to this preparation command.
Normal project configure/build never downloads dependencies. For offline preparation,
place the three pinned source-package files in the cache and omit that flag. The
script checks every SHA-256 before extraction and disables Meson wrap downloads.
The source package's `.dsc` signature is not separately verified; integrity is pinned
by these hashes, recorded from the Ubuntu archive:

| File | SHA-256 |
| --- | --- |
| `systemd_249.11-0ubuntu3.22.dsc` | `722f7ef054c6696d19dd0eb3228668a1807011a7ba04e5cdd22e6c93a82b17d0` |
| `systemd_249.11.orig.tar.gz` | `305ba81cc33593bc2e8e9d6dc7f964b1c0a9303155fced5e6b1d236577441bf2` |
| `systemd_249.11-0ubuntu3.22.debian.tar.xz` | `4ce334b6483e938692c7fa1a29c6c69a518053a62b2b67752d57fcadf24bce57` |

The source is retrieved from the
[Ubuntu source archive](https://archive.ubuntu.com/ubuntu/pool/main/s/systemd/).
Keep the extracted source and its license files with the cache. Public libsystemd
code is LGPL-2.1-or-later; the source package records additional component licenses.
Release source/notice/relinking artifacts and security-update review remain packaging
work. A pinned archive must be rebuilt and the pin reviewed when dependency fixes
are adopted; the pin is not a claim that future security updates are unnecessary.

Default cache location is `$XDG_CACHE_HOME/tired/systemd-249.11-0ubuntu3.22`, falling
back to `$HOME/.cache/tired/systemd-249.11-0ubuntu3.22`. Override it with the same
absolute `TIRED_SYSTEMD_CACHE` value for preparation and project configuration.
The archive lives in `build-x86_64/libsystemd.a` or `build-aarch64/libsystemd.a`.
Preparation is native only. Use separate architecture directories and trusted
source caches; the script does not reset local edits in an existing extracted tree.

Clang builds the archive. The upstream optional C++ probe is disabled; no C++
compiler is required. Upstream generators use their own documented build tools.
Journal XZ, LZ4, Zstandard, gcrypt and audit type-name support are enabled. Optional daemon-focused
SELinux, AppArmor and seccomp configuration is disabled for this archive;
the selected libsystemd/basic sources reference these switches only in their build
feature description. No manager daemon or security policy is replaced.

The native library test creates an sd-bus object and links the journal reader without
contacting a manager or opening the host journal. Its post-link check enforces the
direct glibc-only shared-library allowlist, including the transitive dependencies.
Live sd-bus/journal behavior and compressed-journal interoperability still need their
backend tests. Distribution builds bypass the source cache and preparation script.
