# Snap confinement and review material

The complete product manages arbitrary existing **host** systemd services and
captures the invoking host command/directory. The prepared package therefore uses
classic confinement, with a core22 base matching the direct glibc 2.35 build. No
reduced-functionality or development-confinement package substitutes for it.

## Required host access

| Access | Product operation |
|---|---|
| System manager bus and current-user broker/runtime socket | Verified native queries, reload, jobs and enablement |
| Host journal | Selected-service history/follow and access diagnostics |
| Fixed root-owned helper through host sudo | Explicit authorization, actor preservation and checked system mutation |
| Arbitrary captured executable, working directory and external inputs | Faithful existing application context |
| `/etc/systemd/system`, `/etc/tired`, `/var/lib/tired`, `/run/tired` | Derived persistent units, private revisions, records and locks |
| Actual account home/XDG and native user unit paths | Current-user scope independent of snap revision storage |
| Host NSS and `/proc`/cgroup facts | Workload identity and actual resource/capability ceilings |

`system-observe` and `log-observe` provide observation rather than general authorized
host service installation. `home` does not cover arbitrary administrator/app
locations and native user runtime/state paths. A fixed `system-files` allowlist
cannot express the full captured application context and manager/elevation flow.
Connecting observation interfaces does not grant root writes or arbitrary native
systemd control. Strict packaging must be tested against these exact operations;
it cannot be judged usable merely because its TUI launches. See the
[official interface reference](https://snapcraft.io/docs/reference/interfaces/).

The maintainer should review the [classic confinement process](https://snapcraft.io/docs/reference/administration/reviewing-classic-confinement-snaps/)
and submit the rationale plus actual host qualification. Classic eligibility and
approval are external decisions and are not assumed. No request or upload has
been made by local preparation.

## Build and local qualification

Build natively on each architecture from `snap/snapcraft.yaml` with Snapcraft 7.3
or later. The `enable-patchelf` build attribute configures the staged ELF files for
the core22 runtime, as described by the
[classic linter documentation](https://documentation.ubuntu.com/snapcraft/8.9.5/how-to/debugging/use-the-classic-linter/).
Alternatively, install `snapd` and `patchelf` and run
`cmake --build BUILD_DIRECTORY --target snap-package` after qualifying a direct
Release build. This target stages the complete payload, patches only its frontend
and helper, then invokes `snap pack`.

Inspect `meta/snap.yaml`, ELF loader/dependencies, architecture and release metadata.
Both executables use `/snap/core22/current/lib64/ld-linux-x86-64.so.2` on amd64 or
`/snap/core22/current/lib/ld-linux-aarch64.so.1` on arm64, with an ELF RPATH selecting
`/snap/core22/current/lib/x86_64-linux-gnu` or
`/snap/core22/current/lib/aarch64-linux-gnu`, respectively. glibc remains dynamic
and comes from the installed base. The root helper sanitizes loader variables, so
it uses these embedded paths when invoked directly through host sudo as well.
The direct archive and relinking bundle retain their ordinary host-loader paths;
Snap staging does not modify their build executables.

On a disposable systemd VM, explicitly install a local package with
`sudo snap install --dangerous --classic FILE.snap`. Test actual system and user
creation, native lifecycle/journal, ordinary denied and authorized helper access,
working directory/argv, refresh to a second local revision, removal and actual boot.
Check runtime environment/path changes and verify all generated host files are
outside `/snap`, `/var/snap`, `~/snap` and revision-owned storage. Snap removal must
preserve service units, needed private environments and app files. These tests are
separate from store review and never install an artifact on a development host.

## Maintainer packet

Supply publisher identity, reviewed public source/release URLs, the complete
Snapcraft recipe, both architecture artifacts/checksums, license and corresponding
static-library sources/relinking materials, exact host-access rationale, test logs
and known limitations. Name registration, classic review, signing and publication
are handled only by the maintainer. Recheck the README before claiming availability.
