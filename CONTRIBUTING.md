# Contributing

Use C17 for application code, test fixtures, fuzz entry points and custom tools.
Keep fields in the shared registry and preserve typed validation and provenance
across the CLI, profiles, renderer and UI. Review ownership, error paths, bounds and
cleanup whenever changing an internal API. Never add executable profile hooks,
raw privileged destinations, fake authorization or root-state redirection to a
production build.

## Build and test

On Ubuntu 22.04+/Debian 12+, install Clang/LLVM/LLD, CMake 3.22+, Ninja, pkg-config,
wide ncurses development headers, json-c, OpenSSL and libsystemd development files.
Direct builds also need static libcap, libgcrypt, libgpg-error, liblzma, lz4 and zstd.
See [dependency preparation](docs/systemd-dependency.md).

```sh
cmake -DTIRED_ALLOW_DOWNLOAD=ON -P cmake/PrepareSystemd.cmake
cmake --preset linux-clang-x64-debug
cmake --build --preset linux-clang-x64-debug --parallel 2
ctest --preset linux-clang-x64-debug
cmake --preset linux-clang-x64-release
cmake --build --preset linux-clang-x64-release --parallel 2
ctest --preset linux-clang-x64-release
```

Use the corresponding `arm64` presets on an ARM64 host. Release requires a working
LTO probe and ELF linkage verification for both frontend and helper. glibc stays
dynamic. For a distribution build, configure with
`-DTIRED_DEPENDENCY_MODE=DISTRIBUTION -DCMAKE_INSTALL_PREFIX=/usr`; this mode uses
installed shared libraries and bypasses CPM. Do not silently switch linkage modes.

```sh
DESTDIR="$PWD/build/stage" cmake --install build/linux-clang-x64-release
clang-format -i --style=file src/changed.c include/tired/changed.h
```

Ordinary CTest tests use private temporary roots, broker fixtures and read-only
manager access. They do not mutate real host services. Native lifecycle/user,
package and reboot tests are separate C executables guarded by an explicit
root-owned disposable-VM marker. Reboot also needs `--allow-vm-reboot` and VM
detection. Never place those markers on a development host. Follow
[release validation](docs/release-validation.md).

Sanitizers are enabled with `-DTIRED_SANITIZERS=ON`; fuzz entry points additionally
use `-DTIRED_FUZZING=ON`. `tired_fuzz_seed ABSOLUTE_CORPUS SOURCE_DIRECTORY` creates
valid seed inputs without executing a workload. Do not put generated corpora,
crash artifacts or dependency caches into source history.

## Profiles

Use the [profile contribution template](packaging/profile-contribution.md). Include
primary evidence, exact conditions, expected generated changes, privilege impact,
unknowns and fixtures. Match names passively and describe the weak identity basis.
Keep explicit user choices. New advice must be discoverable in release notes and
must never silently update an installed service's saved snapshot.

## Review

Explain the concrete behavior, relevant failure policy and actual validation.
Keep changes scoped and code modular. Preserve unrelated work. Run meaningful
Debug and Release checks for behavior changes, then review the final diff for
ownership, races, privacy and cleanup. Update canonical documentation when a
contract changes. Publication, signing and downstream submission require the
maintainer's separate authorization.
