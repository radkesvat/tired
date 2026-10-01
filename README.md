# tired

**Turn a Linux command into a service that keeps running after you close the terminal.**

tired helps you run a program in the background, start it when your computer boots,
and restart it if it fails. It is useful for servers, proxies, network tunnels, and
other programs you want to keep running.

Start with the command you already use:

```sh
tired ./program
```

tired shows the settings for you to review and approve, then writes the service
file for you. **systemd**, the service manager used by many Linux systems, runs
your program after setup. tired does not need to stay open.

## Install

**Version 0.1.0 is being prepared. Release downloads are not yet available.**
For now, follow the [build from source instructions](CONTRIBUTING.md#build-and-test).
Once the `v0.1.0` release is published, you can install it with this command:

```sh
sh -c 'f=$(mktemp) || exit; curl -fsSL --proto "=https" https://raw.githubusercontent.com/radkesvat/tired/v0.1.0/install.sh -o "$f" && sh "$f"; s=$?; rm -f "$f"; exit "$s"'
```

[Read the installer](install.sh)
· [Manual installation and uninstall](docs/packaging.md#direct-archives)

The installer downloads tired over HTTPS, checks the downloads against their
checksums, and installs it in `/usr/local`. It does not change existing services.

tired supports **Linux on x86-64 and ARM64**, with systemd 249+ and glibc 2.35+.
See [system requirements](docs/compatibility.md) for details and the
[packaging guide](docs/packaging.md) for Debian, Ubuntu PPA, and Snap preparation.
These packages are not yet published.

## Create your first service

Use a program that you have already installed and configured. It should run in the
foreground: it stays running in the terminal until you stop it. If the program has
a background or daemon option, leave that option off.

Open a terminal in the folder your program should run from. For example:

```sh
cd /path/to/myapp
tired --name myapp -- ./myapp
```

Replace `/path/to/myapp` with your program's folder and `./myapp` with its command.
`--name myapp` names the service. The `--` separates tired's options from your
program's command. Put your program's own options after its command as usual.

Before approving, check:

- The command and the folder it will run from.
- The user account that will run the program.
- Whether it should start now and when the computer boots.
- When it should restart after stopping or failing.

The default settings start the service now, enable it at boot, and restart it after
a failure. Application profiles can suggest different settings. You can change
these during review. Add `--no-tui` after `tired` for a plain text review.

To preview the settings before creating a service, use `plan`. It does not create
the service or run the program:

```sh
tired plan --name myapp -- ./myapp
```

## Manage your services

Use the name you chose when creating the service. These examples use `myapp`.

| What you want to do | Command |
|---|---|
| List services created with tired | `tired list` |
| Check whether a service is running | `tired status myapp` |
| Watch its log messages | `tired logs myapp --follow` |
| Start it | `tired start myapp` |
| Stop it | `tired stop myapp` |
| Restart it | `tired restart myapp` |
| Start it automatically at boot | `tired enable myapp` |
| Stop it from starting at boot | `tired disable myapp` |
| Review and change its settings | `tired edit myapp` |
| Remove the service | `tired remove myapp` |

Stopping a service does not disable startup at boot. Disabling it does not stop a
running service. Editing a running service normally restarts it to apply changes.

Run `tired` with no command to open the service dashboard in a supported terminal.
Use `tired --no-tui` for a simple list.

## Suggested settings for common programs

tired includes **application profiles**: suggested service settings for known
programs. They cover proxies, network relays, DNS and web servers, and other
networking tools. See the [full profile list](docs/profiles.md#bundled-application-profiles).

tired can suggest a profile from the program's filename, without running it.
A matching name does not guarantee that the settings suit your setup; review them
before approving.

Unknown programs use general defaults. Add `--profile none` after `tired` to use
those defaults without an application profile.

## User accounts and permissions

By default, tired creates a system service. Creating it needs administrator
permission, but the program normally runs as the user who called tired. Check the
account shown in the review.

The **Backhaul profile proposes root**, the administrator account, for new system
services. Use `--run-as USER` to choose a different account, replacing `USER` with
an existing username.

To create a service for your own user account instead:

```sh
tired --user ./program
```

Add `--user` when managing that service too, for example `tired list --user`.
Starting a user service before login requires a setting called *lingering*. See
the [user service requirements and setup](docs/walkthroughs.md#user-service-and-lingering).

## If something goes wrong

Start by checking the service and its logs:

```sh
tired status myapp
tired logs myapp --lines 100
tired doctor myapp
```

If a new service fails to start, it stays installed so you can inspect and fix it.
A running process does not always mean the application is working correctly; check
the application itself too.

If a tired operation was interrupted, `tired recover` shows what needs attention.
Follow the [recovery guide](docs/recovery.md) before choosing how to continue.

## Remove a service or uninstall tired

`tired remove myapp` stops the service, disables startup at boot, and removes the
files tired created for that service. It keeps your program, application data,
external configuration files, credentials, and logs. Stopping or removing a network
tunnel you use for SSH access may disconnect you.

Upgrading or uninstalling tired preserves the services it created. They remain
available to systemd. See the [uninstall instructions](docs/packaging.md).

## Learn more

- [Walkthroughs](docs/walkthroughs.md) — examples with scripts, configuration files,
  and user services.
- [Command reference](docs/cli.md) — all options, including JSON output for scripts.
- [Project wiki](wiki/Home.md) — installation and service guides.
- [Security](docs/security.md) — permissions, secrets, and file ownership.
- [Contributing](CONTRIBUTING.md) and [architecture](docs/architecture.md) — building
  tired and understanding its code.

tired uses the [MIT license](LICENSE). See [dependencies](docs/dependencies.md) for
third-party licenses and requirements when distributing static builds.
