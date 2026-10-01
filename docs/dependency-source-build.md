# Corresponding dependency sources

The direct-release source bundle contains Debian-format source descriptions,
upstream archives and distribution patches for the exact dependency versions.
Each `.dsc` lists SHA-256 checksums; the packaging command verifies its referenced
archives before assembling this bundle. `build-info.txt` in each relinking archive
and the binary payload's dependency inventory identify versions and architecture.
Use a native Ubuntu 22.04 environment with the recorded Clang/LLD toolchain.

Extract a source package without downloading anything:

```sh
dpkg-source -x PACKAGE_VERSION.dsc source-PACKAGE
```

For the pinned libsystemd archive, place its three source files in an external
directory and invoke the included preparation recipe with that directory:

```sh
cmake -DTIRED_SYSTEMD_CACHE=/absolute/path/to/cache -P cmake/PrepareSystemd.cmake
```

The recipe verifies the pin, extracts distribution patches, disables wrap downloads
and builds only the native static library. It never replaces a host manager. Its
declared build dependencies are documented in the upstream source and tired's
`docs/systemd-dependency.md` in the companion upstream-source asset.

Build replacement libraries from the extracted source using their upstream build
instructions and the same architecture/toolchain. Typical static-library choices
are `--enable-static --disable-shared` for Autoconf libraries, `-DBUILD_SHARED_LIBS=OFF`
for json-c/CMake, and static library targets
for libcap, LZ4 and Zstandard. ncurses must select wide-character support and the
separate tinfo library. Nettle's SHA-256 needs `libnettle`, without the separate
public-key `libhogweed` library or GMP. Preserve the direct libsystemd reader's
disabled cryptographic backends. Use a private prefix/pkg-config directory for
modified dependencies. No library needs to be
installed over the host's runtime files.

The full source package includes its own authoritative build documentation and
license notices. Utilities inside a source package may use additional licenses;
their presence does not mean they are linked into tired. GCC runtime support uses
its runtime exception; the actual compiler support archive/package is recorded in
the inventory rather than inferred from an unrelated installed shared runtime.

Copy compatible replacement archives into the architecture's relink bundle using
its existing names, then follow that bundle's standalone CMake instructions. Check
both resulting ELF files for the direct glibc-only shared dependency allowlist.
The helper need not be executed or installed to verify relinking. The companion
MIT tired source permits rebuilding first-party objects as well. Release LTO
objects require a compatible recorded Clang/LLD version.
