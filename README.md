# tired

C17 project scaffold. The executable prints a scaffold message; the application
is not implemented yet.

## Build

Supported target: **Linux x86-64 and ARM64, Clang, glibc**, with Debug and Release builds.
Release requires LTO. Project and third-party libraries are linked statically;
**glibc remains dynamically linked**. This is not a
fully static or distribution-independent executable. LLD is the linker.

On Ubuntu/Debian, install the build tools (CMake 3.22 or newer):

```sh
sudo apt-get update
sudo apt-get install clang llvm lld cmake ninja-build binutils pkg-config libjson-c-dev
```

Run from this directory:

```sh
cmake --preset linux-clang-x64-debug
cmake --build --preset linux-clang-x64-debug
ctest --preset linux-clang-x64-debug
./build/linux-clang-x64-debug/tired

cmake --preset linux-clang-x64-release
cmake --build --preset linux-clang-x64-release
ctest --preset linux-clang-x64-release
./build/linux-clang-x64-release/tired
```

On a native ARM64 host, replace `x64` with `arm64` in these commands.
Presets select native builds; they do not install a cross-compilation sysroot.

Both builds check ELF dependencies after linking. Debug keeps debug information
and disables LTO; Release enables optimization and LTO. No C++ compiler is needed.

Presets install into `install/<preset>/` without root privileges:

```sh
cmake --install build/linux-clang-x64-release
```

Generate a development archive with:

```sh
cd build/linux-clang-x64-release
cpack -G TGZ
```

## Dependencies and CI

[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) 0.42.0 is vendored, including its
license, and loaded in direct mode through `cmake/Dependencies.cmake`. The core uses
json-c 0.15 or newer, discovered locally through pkg-config. Builds configure offline
when development packages are installed. Future source dependencies must use pinned
commits or archive hashes and explicit static-library options.
Direct builds use `$XDG_CACHE_HOME/tired/cpm` (or `$HOME/.cache/tired/cpm`);
override with `CPM_SOURCE_CACHE`, prepopulate it, or supply local CPM sources for
offline builds after packages are added.

The GitHub Actions workflow builds, tests, stages, and uploads a development archive
for both configurations on native x64 and ARM64 runners. The ARM64 job uses
[GitHub's `ubuntu-24.04-arm` runner](https://docs.github.com/en/actions/reference/runners/github-hosted-runners). It assumes **this directory is the GitHub repository
root**. No remote repository or release is
created by this scaffold.

## Dependency modes

The default `-DTIRED_DEPENDENCY_MODE=DIRECT` requires static dependency archives.
Use `-DTIRED_DEPENDENCY_MODE=DISTRIBUTION` for Debian/PPA builds using installed
shared libraries. Distribution mode bypasses CPM entirely. Neither mode downloads
runtime dependencies automatically or switches to the other linkage mode.
Direct mode requires a local json-c static archive; distribution mode uses its shared
library. Missing development libraries fail configuration without downloading them.

For example, configure a separate distribution build without changing a preset tree:

```sh
cmake --preset linux-clang-x64-release -B build/distribution-release \
  -DTIRED_DEPENDENCY_MODE=DISTRIBUTION -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/distribution-release
ctest --test-dir build/distribution-release --output-on-failure
DESTDIR="$PWD/build/stage" cmake --install build/distribution-release
```

Development preset installations are confined to `install/<preset>`. Remove that
specific staging directory to uninstall a development copy; this does not remove
any generated service state. Do not use that procedure for a system installation.
