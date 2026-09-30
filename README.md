# tired

Turn an existing foreground command into a persistent, understandable systemd
service. Run it from the directory the program should use:

```sh
tired ./program
```

Review the command, account, working directory and restart policy, then approve.
Systemd supervises the service afterward; tired does not stay running.

## Quick install

This checkout prepares release **0.1.0**. The maintainer has not published the
repository or release assets yet. After publication, the direct installer is:

```sh
sh -c 'f=$(mktemp) || exit; curl -fsSL --proto "=https" https://raw.githubusercontent.com/radkesvat/tired/v0.1.0/install.sh -o "$f" && sh "$f"; s=$?; rm -f "$f"; exit "$s"'
```

[Inspect the installer](https://github.com/radkesvat/tired/blob/v0.1.0/install.sh)
· [Manual installation and uninstall](docs/packaging.md#direct-archives)
· [Build from source](CONTRIBUTING.md)

The installer verifies HTTPS downloads and checksums, installs the frontend,
helper, profiles and documentation into `/usr/local`, and changes no services.
Linux x86-64 and ARM64 with glibc 2.35+ are the direct-release targets. Local Debian
packages, source packages and Snap preparation are described in
[packaging](docs/packaging.md); a PPA, Debian archive package and Snap Store listing
are separate maintainer-controlled publication steps.

## Review before installation

The actual review screen presents populated values and their origins:

```text
tired / review service
sleep.service | system scope | runs as alice
Generic; application requirements unknown

name               sleep                         captured
argv               ["/usr/bin/sleep","60"]        captured
working_directory  /home/alice                   captured
run_as             alice                         captured
scope              system                        default
start              true                          default
enable             true                          default
restart            on-failure                    default
restart_sec        5000000us                     default
retry_policy       persistent                    default

C apply   1-8 pages   P preview   E env   K credentials
W evidence   R risks   H hardening   ? field/origin help
```

There is one ordinary review and approval. Advanced pages expose every supported
field, including resources, hardening, dependencies and process controls. Preview,
profile evidence and three-way configuration comparisons use the same saved model
as headless commands. A plain review is available with `--no-tui`. Running tired
without operands opens the [managed-service dashboard](docs/list.md) when TUI is
enabled and the terminal is usable. Setting `tui` to `false` or using `--no-tui`
selects a compact list; `--json` selects JSON listing.

```sh
tired plan --offline --profile none -- ./unknown-program --port 8080
tired --profile auto -- ./backhaul -c server.toml
tired --profile frpc -- ./frpc -c client.toml
tired --profile frps -- ./frps -c server.toml
tired --name worker -- python3 worker.py
tired create --yes --json --profile none -- ./program --help
tired status worker
tired logs worker --follow
tired edit worker --apply-mode defer --restart-sec 10s
tired rename worker worker-main
tired remove worker
```

Discovery never executes the target. Profiles are declarative recommendations with
sources and uncertainty, not proof of a binary's identity. Unknown applications
use generic defaults. Command arguments are literal; flags after the workload or
its `--` boundary belong to the workload.

## Accounts, boot and failure

System scope is the default. Administrator permission to install a unit does not
turn a nonroot workload into root. An explicit account change is visible and may
require its own [risk acknowledgment](docs/risks.md). Validated sudo invocation
metadata preserves the original user as a default. User scope controls only the
calling user's existing manager:

```sh
tired --user ./program
tired --user --enable-linger ./program
```

Enabling a user unit does not ensure startup before login. `--enable-linger` is an
explicit account-level change, authorized separately. Removing or stopping a
service never disables lingering or silently re-enables a past request.

Start and boot enablement are independent. Persistent retry mode uses delayed
retries without a finite start budget; limited mode has an explicit interval and
burst. Initial process observation is not an end-to-end application health check.
A newly installed service that fails startup remains installed and returns code
7. A failed edit attempts to restore the previous control-plane revision. Inspect
`tired doctor NAME`, status and logs before changing privileges or limits.

## Ownership and removal

Generated units are ordinary systemd services. Upgrading or uninstalling tired
preserves their units, private environment revisions and administrative state.
`tired remove NAME` stops/disables and removes only that managed service's owned
resources. It preserves the program, application data, external config, credentials
and journal. Stopping a networking service may disconnect the SSH route used to
operate it. Interrupted changes remain inspectable with `tired recover` and require
an explicit [recovery decision](docs/recovery.md).

[Command reference](docs/cli.md) · [Walkthroughs](docs/walkthroughs.md)
· [Security](docs/security.md) · [Compatibility](docs/compatibility.md)
· [Architecture](docs/architecture.md) · [Project wiki](wiki/Home.md)

MIT licensed; third-party notices and static-library relinking requirements are
listed in [dependencies](docs/dependencies.md).
