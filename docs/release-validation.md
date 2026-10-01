# Release qualification

This report records local qualification of 0.1.0 on 30 September 2026. Packages
and source uploads are unsigned. GitHub, PPA, Debian archive and Snap Store
publication remain the maintainer's responsibility; none has been submitted.

## Crypto dependency reduction — 1 October 2026

The direct build now uses Nettle for SHA-256 and glibc `explicit_bzero` for memory
clearing. Its pinned libsystemd reader disables OpenSSL, gcrypt and GnuTLS while
retaining XZ, LZ4 and Zstandard. Distribution builds use shared Nettle and the
distribution's shared libsystemd; transitive distribution dependencies remain
package-managed.

A small C comparison program performed streaming SHA-256, printed the digest and
cleared its state. Both candidates used the same Clang `-O3`, ThinLTO and stripping
options on each architecture; libsodium included its documented initialization.
The measurements below are probe executables, not the full application:

| Architecture | Nettle 3.7.3 | libsodium 1.0.18 |
| --- | ---: | ---: |
| x86-64 | 24,440 bytes | 209,552 bytes |
| ARM64 under QEMU | 11,040 bytes | 120,912 bytes |

Both produced the same SHA-256 result. Nettle also avoids process-wide library
initialization for this use. Application tests cover standard SHA-256 vectors,
all two-part splits of short vectors, empty input, a million-byte message, state
clearing and bounded buffer clearing. Existing profile, fingerprint, review,
transaction and recovery tests continue to use unchanged digest formats.

With the same native x86-64 Clang 19 toolchain and Release settings, the unstripped
frontend decreased from 9,348,688 to 3,521,104 bytes (8.92 to 3.36 MiB). The helper
decreased from 5,933,520 to 1,626,152 bytes. Stripping a measurement copy of the new
frontend produced 3,228,472 bytes; release stripping policy was not changed.

Native DIRECT Debug passed its 88-test suite and the subsequently added journal
format test. DIRECT Release passed 89/89 tests. DISTRIBUTION Release passed its
88 applicable tests; the direct installer test was skipped by design. Five focused
ASan/UBSan tests passed for hashing, fingerprints, catalogues, redaction and journal
reading. The format test was rerun after its export timestamp was updated to allow
validation of newly sealed fixtures.

A disposable ARM64 guest produced a sealed Zstandard journal, which the system's
`journalctl --verify --verify-key` successfully authenticated. The new x86-64
DIRECT Debug/Release and DISTRIBUTION readers recovered its complete 16 KiB message.
This validates reading sealed journal data; tired does not authenticate seals.

The x86-64 direct archive, CPack Debian binary, Snap and static relinking bundle
were rebuilt. Both relinked executables passed ELF linkage inspection. A fresh
archive extraction listed the installed profiles. Debian metadata declares
`libnettle8` and no direct OpenSSL/gcrypt dependency. Unsigned Debian and Jammy/Noble
source packages were prepared; the source-only build used `-d` because the host's
versioned Clang/LLVM tools and Snap CMake do not satisfy Debian metapackage checks.
This is not a clean Debian binary rebuild or an archive/store submission.

The corresponding-source bundle now includes Nettle and the same pinned systemd
source, with checksum verification and updated LGPL notices. Workflow syntax was
checked with actionlint. No new Snap installation/confinement qualification is
implied here.

ARM64 application validation was stopped at the maintainer's request because of
the emulation time. The SHA-256 comparison probes and static libsystemd reader
build completed, but the application build and tests remain unverified for this
change. The older ARM64 results below apply to the earlier code only.

## Profile catalogue follow-up — 1 October 2026

The initial expanded catalogue contained 36 profiles, including 34 networking
application names. Native x86-64 DIRECT Debug and Release each passed 86/86 CTests.
Six targeted profile/catalogue tests passed in the x86-64 DISTRIBUTION Release
build. The final system-scope restriction for the root-run deployment passed the
bundle test in all three configurations. A staged Release installation listed all
36 profiles, and offline explanation retained the privilege advisory while
suppressing descriptor limits whose host ceiling was unknown.

This follow-up validates matching, profile semantics, overrides, rendered limits
and installed discovery. It does not execute or benchmark the upstream networking
applications. ARM64 builds/tests and QEMU qualification were not rerun for these
profile changes; the earlier architecture evidence below remains a separate record.

The subsequent Backhaul account-default change passed 87/87 CTests in each native
x86-64 DIRECT Debug and Release build, plus the account, bundle and plan tests in
DISTRIBUTION Release. Tests cover nonroot risk acknowledgment, explicit account and
group overrides, user scope, saved-service refresh, compatibility suppression and
review recomputation. Staged CLI plans rendered `User=root` by default and
`User=nobody` with an explicit account override. No ARM64 or QEMU work was run.

The catalogue was then reduced to 33 profiles by removing the `nc`, `netcat` and
`ncat` profiles. All eight targeted profile, catalogue and planning tests passed in
each x86-64 DIRECT Debug, DIRECT Release and DISTRIBUTION Release build. A fresh
DIRECT Release installation also passed the bundle test against its 33 installed
profiles. ARM64 builds/tests and QEMU qualification were not rerun for this removal.

## Build and test lanes

The direct baseline is Ubuntu 22.04, glibc 2.35, Clang/LLVM LLD, C17 and Release
LTO. Third-party libraries are static; glibc remains dynamic. The pinned journal
client is Debian systemd 252.39-1~deb12u2, built on that baseline, so compact
journals written by newer qualified managers remain readable. The minimum manager
API remains 249. The dependency preparation recipe verifies all three source
archives before extracting the patched source into an external cache.

Distribution packages are native Debian 12 Clang/LLD LTO builds with shared
distribution libraries, `/usr` payload and automatically generated shared runtime
dependencies. No dependency download runs during ordinary configuration or a
distribution build. Exact compiler, library, static compiler-support archive and
source-package identities accompany each artifact in `BUILD-INFO.txt`,
per-architecture dependency inventories and third-party copyright notices.

| Lane | Result |
| --- | --- |
| x86-64 direct Debug, validation host | 85/85 CTests passed, 121.86 seconds |
| x86-64 direct Release LTO, validation host | 85/85 CTests passed, 144.20 seconds |
| ARM64 direct Debug | 84/84 core CTests passed, 257.26 seconds; installer excluded from this batch |
| ARM64 direct Release LTO | 84/84 core CTests passed, 193.54 seconds; installer excluded from this batch |
| Shared Debug ASan/UBSan | 84 passed; direct-only installer correctly skipped, 126.27 seconds |
| Seeded libFuzzer parser campaign | 422,084 executions in 61 seconds, no sanitizer failure |
| Workflow syntax and shell analysis | actionlint 1.7.12 and installer ShellCheck passed |
| Debian amd64 binary/source build | 84 CTests passed with the direct installer skipped, 97.58 seconds; native unsigned source/payload audit and Lintian completed |
| Debian arm64 binary/source build | 84 CTests passed with the direct installer skipped, 119.10 seconds; native unsigned source/payload audit and Lintian completed |
| Clean core22 Snapcraft amd64 recipe | 85/85 CTests passed, 160.35 seconds; packaged frontend and helper select the base loader and libc |
| Final ARM64 input delta | Frontend, settings and five editor scenarios passed in DIRECT Debug (31.41 seconds), DIRECT Release (32.47 seconds) and DISTRIBUTION Release (23.05 seconds) |

ARM64 compilation and execution use an aarch64 guest under QEMU TCG rather than
cross-compilation. Timing on those guests is not a native hardware performance
claim. Both Debug and optimized Release tests exercise the same public contracts.
The ARM64 core suites preceded the final Escape-only correction. The final runtime,
helper and updated PTY driver were then rebuilt and the affected input/presentation
tests repeated. Packaging retained the unchanged core-unit evidence and used the
tested final production code; `nocheck` during the final Debian packaging step
avoids relinking unchanged test executables under emulation.
The installer test serves checksum-verified release fixtures through a local TLS
server and checks reinstall, upgrade, unrelated files, unavailable downloads,
corruption, wrong architecture, invalid TLS, permissions and uninstall. It now
uses the actual CPack payload and also accepts an exact final release archive;
qualification does not substitute a synthetic layout for a delivered archive.

The additional coverage checks authorized profile replacements and their removal,
conditional advice refresh, dashboard settings/observations, typed/unified diffs,
and captured public JSON examples. Real-manager recovery cases interrupt restart
and immediate-edit submission, require durable completion evidence rather than an
old running PID, and verify that an interrupted no-start edit finishes stopped.
The included public corpus contains 77 captured command forms.

Sanitizer and clean recipe runs exposed two resize/input races during preparation.
Deterministic PTY regressions reproduce a missed resize notification and Escape
consumed during a resize. The corrected input path preserves cancellation and
split UTF-8 input, blocks hidden edits, and restores terminal state. Frontend and
all five editor scenarios passed three consecutive Debug, Release and sanitizer
runs. Initial failures remain separate from successful qualification logs.

## Native systemd hosts

Disposable cloud-image VMs provide real systemd, journal, PAM login, sudo and
boot behavior. Host administration is never redirected by a product environment
variable. Images are verified against their publisher's checksums before boot.
The drivers require root and an administrator-owned private
`/opt/tired-tests/disposable` marker. Reboot additionally requires the explicit
`--allow-vm-reboot` argument and a detected VM.

| Host | Architecture | Manager | Qualification |
| --- | --- | --- | --- |
| Ubuntu 22.04 | amd64 | 249 | System/user scope, boot, classic Snap and strict-confinement assessment |
| Ubuntu 24.04 | amd64 | 255 | System/user scope, boot and compact journal reading |
| Debian 12 | amd64 | 252 | System/user scope, boot, Debian lifecycle and adversarial mutations |
| Ubuntu 22.04 | arm64 | 249 | System/user scope, actual boot, adversarial mutations, journal rotation and classic Snap removal/boot |
| Ubuntu 24.04 | arm64 | 255 | System/user scope, actual boot, adversarial mutations and compact journal rotation |
| Debian 12 | arm64 | 252 | System/user scope, boot, Debian upgrade/removal/purge/reboot, adversarial mutations and journal rotation |

Boot checks inspect the enabled system fixture and explicitly lingering user's
fixture before any login by the workload account. An SSH login as root retrieves
the observations; it does not activate that account's manager. User qualification
first opens a real loopback SSH/PAM session, tests authorization denial and an
explicit temporary helper authorization, then removes that temporary policy.
The release installs no authorization policy.

The session fixture waits for actual output from a C workload running as UID 1501
through SSH/PAM before disabling historical lingering, and holds that session until
cleanup. An earlier fixed-delay fixture raced slow ARM login startup; it was
corrected rather than represented as a product authorization failure. Compilation
also caused one PTY manager-probe skip and a package-driver timeout under emulation;
the affected checks passed when rerun without competing compilation. These initial
results are retained separately from successful qualification.

After the behavior repairs, both Ubuntu 24.04 architectures repeated native
restart/edit recovery, retry/readiness, user authorization/identity, compact-journal
rotation and actual frontend/editor checks with the new baseline-built binaries.
Both Debian architectures repeated native lifecycle/adversarial/recovery cases,
exact package reinstall/version-upgrade/removal/purge and real system/user boots,
including a second boot with the frontend absent. User-boot observations were
retrieved through root login without logging in as the workload account.

## Acceptance scenarios

The evidence below maps the product acceptance scenarios to focused C tests and
real-host drivers. A focused test is not a claim to have physically cut power at
every CPU instruction or to have tested every possible hostile filesystem.

| Scenario | Evidence and observed contract |
| --- | --- |
| A01 Generic executable | Proposal/model/profile tests and native generic loop creation; usable defaults without guessing application privileges |
| A02 Shipped application profiles | Catalog/profile/executable-evidence tests for Backhaul, frpc and frps; explicit overrides survive merging |
| A03 Invocation directory | Capture tests and native fixture report preserve the original working directory |
| A04 Spaces and quotes | Encode/render tests plus actual native argv report preserve boundaries |
| A05 Empty and literal arguments | Capture/encode/render tests and native empty, dollar, percent, backslash, semicolon, tab and newline fixture arguments; invalid input rejected |
| A06 Workload option boundaries | CLI tests pass target flags after the command boundary without tired interpreting them |
| A07 Interpreter/module/JAR naming | Name/capture/proposal tests preserve command bytes while deriving useful names |
| A08 Version-switching symlink | Capture/evidence tests retain the lexical path and inspect the resolved identity; changed identity requires explicit edit |
| A09 Foreign name collisions | Name-selection/load-path/publication tests cover vendor units, masks, aliases, dangling links and drop-ins |
| A10 Concurrent automatic names | Two actual native create processes commit distinct names under the shared scope lock |
| A11 Post-review conflict | Final-name and transaction tests revalidate under lock; interactive approval is invalidated, headless automatic creation selects a new suffix |
| A12 Ordinary user/system service | Native user driver separates helper authorization from the preserved nonroot workload UID/GID |
| A13 Direct root | Native root creation, risk inventory and populated review disclose root execution |
| A14 Filename privilege advice | Profile/evidence/risk tests refuse silent root/capability recommendations |
| A15 Missing approval/authorization | CLI/helper/PTY tests and actual native denied sudo exit without service mutation |
| A16 User without linger | Native user driver observes login-dependent behavior without modifying account lingering |
| A17 Explicit linger/boot | Actual user boot check verifies the enabled fixture without an account login |
| A18 External process death | Native system driver kills the workload and observes systemd replace its PID |
| A19 Explicit stop | Native stop under persistent restart remains stopped while boot enablement stays separate |
| A20 Persistent retries | Native fail-times fixture exceeds a typical finite budget and reaches running state |
| A21 Limited retries | Native limited fixture exhausts its configured burst; the observed failure/count remains visible |
| A22 System boot | Enabled native fixture survives an actual guest reboot |
| A23 Package removal/boot | Debian and classic Snap drivers remove package payload, restart the preserved unit/environment and perform an actual reboot |
| A24 Enable/start combinations | Native driver and transaction tests distinguish disk, boot and runtime states |
| A25 Initial startup failure | Native invalid runtime fixture preserves installed/enabled state and reports failure honestly |
| A26 Invalid active edit | Native edit and transaction tests restore the previous control-plane revision and report runtime restoration |
| A27 Deferred edit | Native driver installs the new revision and identifies that the running context still uses the previous revision |
| A28 Filesystem failures | Test-only wrapped write/fsync/rename inject short write, ENOSPC, EIO and EACCES; publication and recovery preserve before-state |
| A29 Process death by phase | Controller children receive actual SIGKILL around bootstrap, publication, reload, enable, activation, observation, record, commit and cleanup boundaries; explicit reconciliation is repeatable |
| A30 Frontend/SSH loss | Native notify fixture starts after the frontend is killed; the independently bounded helper completes its durable transaction |
| A31 Malicious filesystem replacement | Directory/private-file/publication/fingerprint tests reject changed bindings, symlinks, hardlinks and unsafe ancestry |
| A32 External edit/drop-in | Native adversarial driver refuses foreign content, requires explicit backed-up restoration and reports drop-in conflicts |
| A33 Environment grammar | Environment parser/snapshot tests and native fixture verify assignment semantics without shell execution |
| A34 Environment precedence | Actual native report receives the documented explicit/live/file precedence |
| A35 Secret handling | Redaction/export/protocol/storage/PTY tests protect classified argv/environment values and private persistence |
| A36 User NOFILE ceiling | Actual user manager refuses an infinity request above its ceiling with `nofile-ceiling`; no manager/global setting changes |
| A37 Partial environments | Broker/native access tests report disconnected or absent managers and inaccessible/unsupported journal files instead of fabricated state |
| A38 Terminal behavior | PTY tests exercise resize, narrow/plain fallback, Escape, Ctrl-C and terminal restoration |
| A39 Terminal controls | UI/profile/log rendering tests escape untrusted control bytes |
| A40 Rename failure | Native adversarial rename restores the old identity and avoids duplicate active processes |
| A41 Owned removal | Native remove and manifest tests remove owned resources while leaving application/configuration/data intact |
| A42 Profile update | Snapshot/profile mutation tests preserve existing services until explicit refresh/review |
| A43 Incompatible schemas/helper | JSON/model/protocol tests refuse unsupported major versions and invalid envelopes before mutation |
| A44 Sustained display/logs | PTY cancellation tests and measured sustained dashboard/log sessions; bounded record/display buffers and actual journal rotation |
| A45 Missing executable/path | Doctor/access/evidence tests report missing or changed paths without promising automatic repair |
| A46 Production feature separation | Release build has no test injection definitions, no root-state redirection, no fake authorization and ordinary non-setuid helper payload |

Transaction failure injection is linked only into dedicated test executables.
Test backends operate on temporary directories through explicit C interfaces.
The installed frontend/helper contain the real backend. SIGKILL evidence covers
durable protocol boundaries; sudden storage-device loss and faulty fsync hardware
are outside this software test's claims.

## Static analysis

Clang 19 scan-build intercepted a shared Debug build and emitted 29 findings.
They were reviewed against their actual caller and output contracts rather than
reported as a zero-warning run. No blocking defect remained from these findings:

| Findings | Disposition |
| --- | --- |
| main/CLI parse, 2 dead stores | Nonfunctional assignments; optional cleanup |
| capture path join, 1 null argument | Caller supplies a captured nonempty absolute directory |
| identity selector, JSON unicode, job ID, 3 uninitialized values | Successful parser paths assign every output; error helpers always return false |
| unit redaction, 3 null accesses | Positive secret counts imply allocated capacity derived from bounded argv/environment counts |
| inspection deadline, lingering, name query, 8 uninitialized values | Successful clock/deadline helpers assign outputs; every failed path exits or returns false |
| private file binding, 1 negative descriptor | Only successfully opened or checked directory descriptors reach binding |
| service metadata, 5 value/string findings | Successful strict typed parser/text-copy paths assign all outputs before use |
| publication rollback, 1 null output | Successful reopen allocates and publishes its output; all failed paths return false |
| journal record decoding, 2 uninitialized values | Successful field decoding assigns presence, data and length on both present/absent paths |
| review backspace, 2 null accesses | Buffer's positive length implies its allocated data; clearing preserves allocation |
| edit cwd, 1 getcwd argument | `getcwd(NULL, 0)` is the supported glibc allocation extension on the declared Linux baseline |

ASan/UBSan and seeded fuzzing supplement this review. LeakSanitizer suppression
is limited to the documented ncurses process-lifetime terminal caches in PTY tests;
first-party allocations remain checked. Fuzzing covers bounded JSON, argv,
environment, profiles, settings, model, record and transaction parsing. This is
finite evidence, not a guarantee that no defect exists.

## Measured interactive resource use

`tired_measure` opens a 100-column PTY, waits for the review/dashboard marker or
first log event, drains output, samples `/proc` RSS and CPU counters, and cancels
through the terminal. The updated review/dashboard observations use an x86-64
Clang 19 Release validation build while native package builds and two emulated
ARM guests shared this four-CPU host. The journal observation is retained from
the qualified Ubuntu 24.04 guest. These are measurements under load rather than
a benchmark guarantee for the Clang 14 release assets.

| Session | First output | Duration | Peak frontend RSS | Frontend CPU |
| --- | --- | --- | --- | --- |
| Populated generic review | 515.808 ms | 3 s | 7,968 KiB | 0.000 s |
| Idle dashboard | 15.412 ms | 90 s | 5,032 KiB | 0.000 s |
| Actual Ubuntu 24.04 journal follow | 11.039 ms | 180 s | 12,000 KiB | 0.340 s |

The dashboard marker measures its first frame; asynchronous inventory completion
depends on the host manager and service count. CPU counters cover the frontend,
not transient collector children. Log RSS was 11,808 KiB at 30 seconds and 12,000
KiB at 180 seconds, including native journal mapping/cache warmup. This finite
run does not claim identical RSS at every sample or unlimited-duration proof.
Record, line and display storage are bounded independently of history length.

## Packages, confinement and rebuilds

Debian install/reinstall/version-upgrade/remove/purge checks preserve generated
units, private environment revisions and service identity. A qualification-only
`+localtest1` package supplies a genuine higher version; it is not a release asset.
Lintian reports `initial-upload-closes-no-bugs` because no Debian issue has been
opened, and `no-debian-changes` because upstream's source includes the maintained
Debian recipe. Neither is represented as archive acceptance. Signing and any
future upstream/Debian archive split remain explicit maintainer submission steps.

Classic Snap checks exercise actual root/user host operations, helper
authorization, two local revisions, removal and boot independent of the package.
A separately repacked strict test with observation interfaces fails host creation
and creates no unit. That test package is excluded from release assets. The
[classic rationale](snap-classic.md) records the required host access
and external review requirement.

A loaded ARM64 Snap features run reached the backend's fixed five-second query
deadline. Inspection found the notify service running with its committed record,
matching unit and no pending transaction. One unchanged rerun passed after idle
guests were stopped to restore memory headroom. Initial timeout, inspected state
and successful logs are retained; no runtime deadline was relaxed.

The source delivery excludes builds, caches, local policy/plans and private VM
keys. Corresponding dependency sources include verified distribution source
archives/patches. Static relink bundles include both executable object sets,
project and third-party archives, notices and a CMake link recipe. Rebuilt
frontend and helper ELF dependencies are checked; the privileged helper is not
executed merely to validate relinking.

## Reproducing qualification

Start with the [build instructions](../CONTRIBUTING.md), select the intended
dependency mode and run both native architecture presets. Tests do not implicitly
reboot a host or install packages. `tired_vm_runner` creates a private disposable
overlay from a separately verified image; keep the runner open while using it.
Its `TIRED_VM_MEMORY_MB` option belongs only to that uninstalled test utility.

Inside an explicitly prepared disposable guest, install the complete payload,
copy native C fixture/drivers into `/opt/tired-tests` and create its private
administrator-owned marker. Run `tired_native_test first ABSOLUTE_FRONTEND` and
`tired_advanced_test features|adversarial|system|user|journal|recovery ABSOLUTE_FRONTEND`.
The journal mode reads actual fixture records before and after `journalctl
--rotate`. The reboot mode requires the additional `--allow-vm-reboot` opt-in.
After reconnection run `after-reboot` and `user-after-reboot` and inspect login
history. Package tests require explicit absolute local package filenames.

Keep qualification logs and disposable VM keys under ignored `build/`; do not
publish keys or test authorization policy. The source contains the fixtures and
finite reproducible procedures; publication credentials are unnecessary for local
validation.
