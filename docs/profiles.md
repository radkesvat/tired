# Profile schema and validation

Profiles are declarative JSON documents with schema_version 1. The structural
schema is `schemas/profile.schema.json`; the C loader adds byte limits and typed
semantic validation. A complete validation must use the C loader, not only a generic
JSON Schema validator. The input budget is 256 KiB, with at most 64 basenames,
128 sources/advisories, and 256 recommendations.

Each recommendation names a supported registry field, typed value, applicable scopes,
automatic/suggestion application mode, strength, risk, reason, and nonempty evidence
references. Sources have unique IDs, HTTPS references, valid calendar dates, and an
evidence kind. Every source reference must resolve. Evidence metadata is not proof
that upstream advice has actually been verified; shipped profiles require review.

Recommendation entries cannot change identity, command, or working-directory fields.
The optional `default_run_as: "root"` metadata selects UID 0 only for a freshly
captured system-service proposal without an explicit account choice. It does not
apply during edits/refreshes or in user scope; normal privilege review still applies.
Capability recommendations must be suggestions, never automatic grants. Unknown
fields and hooks fail validation. Scalar/list values pass through the same typed
model validator used by CLI assignments. Cross-field/host checks still apply after
recommendations are selected.

Conditions contain one predicate or an all/any collection, with at most eight levels
and sixteen children per collection. Predicates cover scope, service type, exact
argument presence, named host features, and named passive inspections. Features and
inspections have closed vocabularies in the schema. Required recommendations need
conditions. Exact-version profiles carry a version string; version-independent
profiles use null. Unknown version evidence must suppress automatic application.

Basename matching is passive and exact, with optional ASCII case folding controlled
by each profile. A match is a hint, not binary authentication. An empty basename
list is suitable for a generic profile. No executable probing occurs.

The loader owns its parsed document and typed recommendation values; metadata views
remain valid until profile destruction. Failed parses preserve the previous profile.
This layer does not establish trust in profile file locations or apply advice.
The catalog and merger provide the discovery and decision layers described below.
Explicit local installation/removal and `edit --refresh-profile` use these same
validated documents. Installation does not modify existing service snapshots.

## Recommendation evaluation

The merger records one disposition per recommendation: applied, scope mismatch,
false/unknown condition, unknown version, incompatible version, suggestion, user
override, conflicting recommendations, or required conflict. Records align with the
profile's recommendation order, preserving its reasons and source references.

Host facts are explicitly true, false, or unknown. All/any conditions use three-state
logic; missing observations do not become false claims or successful checks. The
caller supplies the manager version or a declared offline target baseline. Exact
application versions must come from trusted passive evidence; absent evidence
suppresses automatic application. No version probe is run.

Type recommendations are resolved before other fields. Remaining type predicates
see that selection, including a user's explicit override. Conflicting eligible
recommendations for the same field retain the input value and are reported together.
Equal recommendations can share the same applied result. Explicit user values remain
unchanged; an incompatible required recommendation gets a distinct warning disposition.

Changing a condition input during review, such as service type or arguments,
reevaluates the retained profile snapshot. Advice whose condition no longer holds
returns to the underlying captured or configured default, and the displayed
disposition changes with it. Explicit values and explicitly cleared inheritance
remain user choices. Ordinary editing retains the saved profile document; only
`--refresh-profile` selects newly installed advice.

The merger builds a separate owned model, resolves dependent retry/scope defaults,
and performs current semantic checks before publication. Failures preserve previous
outputs. It does not authorize capabilities, certify profile-file trust, validate
host resource ceilings, or implement installed-service refresh by itself. Condition
evaluation and merge feed both frontend provenance views. Explicit inheritance is
retained as a user choice. Live planning and helper validation establish applicable
host facts and risks independently of profile claims.

## Bundled application profiles

The bundle contains 33 profiles: generic, Backhaul, and the 31 application names
below. Related executable aliases belong to one profile so automatic matching
stays unambiguous. Generic adds no application requirements.

The bundle focuses on programs with a clear use as a persistent server, proxy,
relay or tunnel. Suitable unattended `nc`, `netcat` and `ncat` commands can still
use generic defaults, without a bundled profile or a special descriptor limit.

These profiles are for an existing command that can stay in the foreground.
Several upstream packages already install services; check for an existing service
and listener before creating another one. A profile does not install an application,
write its configuration, add missing command arguments, change a saved service account, or
prove that the selected workload is healthy.

### Descriptor policy

For the Linux proxy, relay and server profiles marked **automatic**, both
`nofile.soft` and `nofile.hard` are recommended at **1048576**. This is tired's
capacity policy, not a claim that every upstream requires that exact number.
[CoreDNS](https://github.com/coredns/deployment/blob/master/systemd/coredns.service),
[Caddy](https://github.com/caddyserver/dist/blob/master/init/caddy.service) and
[rathole](https://github.com/rathole-org/rathole/blob/main/examples/systemd/ratholes.service)
provide service examples with this limit; other upstreams choose different values.
Go's Linux network poller and the Rust Mio poller use epoll. The source references
in each profile distinguish runtime evidence, upstream usage and tired policy.
A renamed or customized binary still needs review.

Both recommendations require `nofile_at_least: 1048576`. If the actual usable
kernel/manager/account ceiling is lower or unknown, both are suppressed. In
particular, a nonroot user manager can have a lower inherited hard limit. Offline
plans leave this fact unknown. No sysctl, global limit or user-manager limit is
changed. Inspect the suppressed advice and select a suitable explicit value, such
as `--nofile 65536:65536`, after checking the available ceiling.

The **suggestion** profiles offer a 65536 pair for review without applying it.
Their implementations, helper programs or workload modes do not justify a universal
high soft limit. The [systemd limit reference](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.exec.xml)
warns that Linux `select()` cannot handle descriptors above 1023. Verify the actual
build before accepting a higher soft limit.

A descriptor ceiling does not reserve that many descriptors or guarantee that many
connections. A proxy commonly needs descriptors for both ends of a connection,
plus logs and other files. Capacity also depends on memory, application admission
limits, system-wide file availability, socket buffers, ephemeral ports, CPU and
network capacity. Profiles do not impose speculative memory/task limits or tune
those host settings. Load-test the actual application and monitor it separately.

### Catalogue and foreground commands

The table lists invocation shapes, not commands ready to paste into production.
Keep your existing endpoints, authentication and configuration paths. Each profile
contains more detailed advice and dated source references, visible through
`profiles show ID`, `profiles explain -- COMMAND ...` and the review's full-profile
view. **Automatic** and **suggestion** below describe descriptor limits only.

| Profile ID | Executable names | Descriptor policy | Foreground workflow and primary reference |
| --- | --- | --- | --- |
| `xray` | `xray` | Automatic | [Run](https://xtls.github.io/en/document/command.html) with existing config; exclude test/key-generation commands. |
| `sing-box` | `sing-box` | Automatic | [Run](https://sing-box.sagernet.org/configuration/); preserve config/data directories and review TUN privileges. |
| `waterwall` | `waterwall`, `WaterWall`, `Waterwall` | Automatic | [Existing configuration directory](https://radkesvat.github.io/WaterWall-Docs/docs/getting-started/installation); this profile targets a root-run system deployment. |
| `mihomo` | `mihomo` | Automatic | [Foreground proxy](https://wiki.metacubex.one/en/startup/service/); preserve `-d`/`-f`. |
| `clash-meta` | `clash-meta` | Automatic | [Legacy name in the Mihomo family](https://wiki.metacubex.one/en/startup/service/); arbitrary `clash` binaries do not match. |
| `v2ray` | `v2ray` | Automatic | [Run](https://www.v2fly.org/en_US/guide/command.html) using the installed version's config syntax. |
| `hysteria` | `hysteria`, `hysteria2` | Automatic | [Client/server mode](https://v2.hysteria.network/docs/getting-started/Server/); retain version-appropriate configuration. |
| `tuic` | `tuic`, `tuic-client`, `tuic-server` | Automatic | [Rust implementation family](https://github.com/Itsusinn/tuic); select the existing role and config. |
| `trojan-go` | `trojan-go` | Automatic | [Existing `-config` invocation](https://github.com/p4gefau1t/trojan-go); preserve TLS and fallback paths. |
| `sslocal` | `sslocal` | Automatic | [Shadowsocks Rust client](https://github.com/shadowsocks/shadowsocks-rust/blob/master/README.md); avoid daemon mode and review its own `nofile` setting. |
| `ssserver` | `ssserver` | Automatic | [Shadowsocks Rust server](https://github.com/shadowsocks/shadowsocks-rust/blob/master/README.md); `ss-server` is a different executable. |
| `gost` | `gost` | Automatic | [Existing chain/configuration](https://gost.run/en/getting-started/quick-start/); retain endpoints and selected transports. |
| `brook` | `brook` | Automatic | [Serving subcommand](https://github.com/txthinking/brook); link generation is finite work. |
| `naiveproxy` | `naiveproxy`, `naive` | Suggestion | [Naive client](https://github.com/klzgrad/naiveproxy/blob/master/USAGE.txt); a Caddy-based server uses `caddy`. |
| `shadow-tls` | `shadow-tls` | Automatic | [Client/server](https://github.com/ihciah/shadow-tls/wiki/How-to-Run); keep any proxy backend independently available. |
| `realm` | `realm` | Automatic | [Network relay](https://github.com/zhboner/realm); omit `-d`/`--daemon`. |
| `rathole` | `rathole` | Automatic | [Existing role/config](https://github.com/rathole-org/rathole/blob/main/examples/systemd/ratholes.service); no role is inserted. |
| `frpc` | `frpc` | Automatic | [Existing FRP client](https://gofrp.org/en/docs/setup/); retain `-c` and its working-directory context. |
| `frps` | `frps` | Automatic | [Existing FRP server](https://gofrp.org/en/docs/setup/systemd/); system-scope network ordering is retained. |
| `chisel` | `chisel` | Automatic | [Client/server](https://github.com/jpillora/chisel); preserve authentication and server fingerprints. |
| `cloudflared` | `cloudflared` | Automatic | [Foreground tunnel](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/do-more-with-tunnels/local-management/as-a-service/linux/); exclude service-install/login commands. |
| `socat` | `socat` | Suggestion | [Persistent relay](https://repo.or.cz/socat.git); review fork behavior and avoid terminal-dependent STDIO endpoints. |
| `caddy` | `caddy` | Automatic | [Use `run`](https://caddyserver.com/docs/running), not backgrounding `start`; preserve certificate storage. |
| `traefik` | `traefik` | Automatic | [Existing `--configFile`](https://doc.traefik.io/traefik/reference/install-configuration/boot-environment/); preserve provider/ACME access. |
| `coredns` | `coredns` | Automatic | [Existing `-conf`/Corefile](https://coredns.io/manual/toc/); review port 53 and upstream DNS loops. |
| `dnscrypt-proxy` | `dnscrypt-proxy` | Automatic | [Foreground proxy](https://github.com/DNSCrypt/dnscrypt-proxy/wiki/Installation-linux); exclude service-management commands. |
| `mosdns` | `mosdns` | Automatic | [v5 `start -c ... -d ...`](https://raw.githubusercontent.com/IrineSistiana/mosdns/v5/coremain/run.go); do not wrap `service`. |
| `iperf3` | `iperf3` | Suggestion | [Foreground server](https://software.es.net/iperf/invoking.html); avoid daemon mode; clients and one-off servers are finite. |
| `autossh` | `autossh` | Suggestion | [Foreground monitor](https://www.harding.motd.ca/autossh/) without `-f`; configure noninteractive SSH authentication. |
| `ssh` | `ssh` | Suggestion | [Foreground `-N` tunnel](https://man.openbsd.org/ssh.1) without `-f`; preserve verified host keys and account access. |
| `nginx` | `nginx` | Automatic | [Foreground master](https://nginx.org/en/docs/ngx_core_module.html) with `daemon off;`; review worker limits and Linux epoll support. |

New profiles retain the existing on-failure restart policy and delay. They do not
turn successful finite commands into automatic restart loops. Use `--restart no`
for finite operations, or explicitly select `--restart always` when a reviewed
long-running workload must retry clean exits too. SSH liveness options and
noninteractive authentication must be supplied in the existing command/config;
tired does not add them. nginx additionally recommends `SIGQUIT` for graceful
shutdown; review the stop timeout for long-lived connections.

### Root-run deployment

The WaterWall profile targets the requested privileged system deployment; its
descriptor recommendations apply only in system scope. Select
the account explicitly during creation, for example from its configuration directory:

```sh
tired --system --run-as root --profile waterwall ./WaterWall
```

Review the root-risk prompt and executable/configuration ownership before approval.
For an approved headless flow, use the ordinary `--allow-risk run-as-root` mechanism
when required. The WaterWall profile does not select an account automatically. It cannot grant
capabilities or turn user scope into a root service. The advisory describes the intended deployment; it is
not an automatic identity change or an enforced application-specific root check.
Other profiles leave low-port, TUN, raw-socket and provider access decisions to the
actual configuration and the normal privilege review.

### Backhaul account default

The Backhaul profile sets `default_run_as: "root"` as deployment policy. A new
system service created with `tired ./backhaul -c config.toml` therefore proposes
UID 0 and its primary group. `--run-as USER` overrides the account default;
`--group GROUP` independently preserves an explicit group. This is visible in
planning and review, and root-risk acknowledgment and helper authorization still
apply for a nonroot invocation. No executable is started merely to match a profile.

User scope keeps the invoking account. Selecting `--profile none` disables this
profile default. Editing, refreshing or restoring an existing service retains its
saved account; changing an existing service to root requires an explicit account
edit. The creation-time account default survives ordinary review recomputation
without overriding subsequent user edits.

### Evidence, revisions and validation

The additional sources were reviewed on 2026-10-01. FRP profiles are revision 2:
the descriptor policy is new; their prior continuous-restart behavior remains.
Existing services retain saved profile snapshots until `edit --refresh-profile`
is explicitly reviewed. Backhaul is revision 2: new system-service proposals default to root, while
explicit account/group choices, user scope and saved identities remain unchanged.
Its existing upstream-based recommendations remain: continuous restart, a three-second delay, system network ordering
and the same host-checked descriptor pair.

The native bundle test loads every shipped file, checks every declared alias for a
unique match, and covers unknown/insufficient/sufficient ceilings, conservative
suggestions, both scopes, explicit overrides/inheritance and rendered limits.
These tests exercise tired's profile behavior; they do not execute the upstream
applications or establish throughput, compatibility of arbitrary builds, or
production capacity. Explicit `profiles validate FILE` works without installed
bundled data.

## Catalog trust and selection

The directory loader accepts only an explicitly supplied absolute directory and
never searches the working directory. It walks ancestors through directory file
descriptors with no symlink following. Every component must be owned by root or the
specified trusted owner and must not be group/other writable. Files must be regular,
have one link, and pass the same owner/write checks. Trusted-root selection is the
caller's responsibility; a bundled label alone is not proof of root ownership.

Files are opened relative to the retained directory descriptor, read within the
profile limit, validated, and hashed with OpenSSL EVP SHA-256. The digest identifies
content, not publisher authenticity. Catalog records retain path, origin, and trusted
owner. Directory order is deterministic; scans stop at 4096 entries and catalogs at
256 profiles. On failure newly appended entries are removed and previous entries
remain. Borrowed record pointers must not survive an append attempt.

Explicit ID selection supports renamed workloads. Automatic matching returns an
ambiguous result when multiple non-generic profiles match, with no arbitrary winner.
Generic is used only as fallback. User-origin entries are excluded in system scope.
Same-ID entries within one origin fail closed. A trusted local replacement must
declare `"replaces":"ID"` matching its own ID and is selected ahead of the
bundled entry; in user scope a matching user replacement takes precedence over
administrator and bundled entries. Selection and listing retain the replacement's
actual origin, path and digest rather than treating it as bundled data. Missing or
mismatched replacement metadata remains a conflict.

`profiles install FILE --yes` explicitly validates and publishes only the owned
local destination. Replacing an existing profile requires matching `replaces`
metadata. `profiles remove ID --yes` removes that local override, revealing the
next applicable profile, including the bundled profile. It cannot delete a
package-owned bundled file. Existing services retain their snapshots until an
explicit refresh. Frontend discovery uses the configured installation data
directory, administrator directory, and user configuration directory in user scope.
Offline output lists ambiguity candidates instead of selecting a winner.

## Read-only profile commands and plans

`profiles list` displays available IDs and names; JSON includes source paths, origins,
and content digests. `profiles show ID` displays the selected document. `profiles
validate FILE` validates an explicitly selected file without a manager or catalog
trust claim. Explicit local install/remove validates its owned destination;
`edit --refresh-profile` reevaluates advice after review. Existing service snapshots
remain unchanged by catalog updates.

Offline planning defaults to auto matching after generic capture and explicit
selection. `--profile none` skips discovery. A selected profile is independently
snapshotted into the plan, so its reasons and source metadata survive catalog cleanup.
JSON output includes the snapshot, digest, source, origin, and each recommendation's
disposition. Text output explains recommendations and cites evidence. Changing profile
selection requires rebuilding the input proposal to avoid retaining stale advice.

Offline evaluation declares systemd 249 as the target baseline; this is not an observed
host version. Features, application versions, and usable NOFILE ceilings remain unknown
unless independently supplied by a validated context. Conditional advice stays visible
but is suppressed when its evidence is unknown. No manager queries or target probing
are performed by this path.

Development binaries use their configured install prefix for bundled data. Run
`cmake --install build/<preset>` to stage the executable and profiles. No source-tree
or current-directory fallback is compiled into production. `profiles validate FILE`
and `plan --profile none` work without installed bundled data.

A missing required directory reports `profile-directory-missing`. When the missing
directory belongs to the bundled profiles, discovery reports `profile-bundle-missing`
with instructions to install the complete package. Optional administrator/user
directories may be absent. Permission and trust failures remain errors; they do
not fall back to generic settings or discard the existing catalogue.

`tired profiles explain [options] -- COMMAND [ARG...]` evaluates a captured invocation
without executing it or writing files. The explicit `--` is required; everything
after it belongs to the workload, including strings that resemble frontend flags.
Creation options such as `--profile ID`, `--user`, `--restart`, and `--set` let you
inspect how explicit selections affect advice. `--json` emits the evaluated proposal
with `command: "profiles explain"`, match candidates, retained profile evidence and
recommendation dispositions. Text output includes the match basis, source, digest,
evidence and suppressed advice. Both use ordinary plan redaction; unknown secret
forms may evade that heuristic. Output-file and sensitive-export options are not
accepted for this command.

Explanation uses the declared systemd 249 baseline and leaves unobserved host facts
unknown. It does not establish live compatibility or installation readiness. Use
`profiles show ID` to inspect a profile document without a workload. Explain with
`--profile none` bypasses catalog discovery and shows the generic configured proposal.

Plans with matching enabled and `profiles list/show` also load additional directories
from administrator [settings](settings.md). Those directories must exist and pass
root ownership and permission checks; they do not permit same-origin duplicates or
grant user profiles system trust. Discovery preserves the prior catalog if any
location fails. Standalone file validation does not load installed settings.
