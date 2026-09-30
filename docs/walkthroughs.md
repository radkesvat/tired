# Walkthroughs

Run tired from the directory the workload should use. The examples use ordinary
foreground commands and placeholders, never real credentials. Inspect the populated
review before approval. Install administrator-owned tired payload before creating
system services as an ordinary user.

## Generic binary

```sh
cd /opt/myapp
tired --name myapp --profile none -- ./myapp --port 8080
tired status myapp
tired logs myapp --follow
```

The generic profile does not infer a daemon protocol, capabilities or app health.
The captured executable is absolute while the invocation directory is preserved.

## Backhaul with an existing config

```sh
cd /opt/backhaul
tired --profile backhaul -- ./backhaul -c server.toml
```

The config remains your application's file. The review shows profile sources,
conditions and selected recommendations; filename matching alone does not prove
binary identity or a need for root. Choose an accessible workload account and keep
explicit overrides. Limits that exceed the selected manager's ceiling require an
explicit compatible setting or inheritance, not a global limit change.

## FRP client and server

```sh
cd /opt/frp
tired --profile frpc -- ./frpc -c client.toml
tired --name frp-server --profile frps -- ./frps -c server.toml
```

Review the client/server recommendation separately. Privileged listening ports or
application modes can require explicit capabilities or account choices. Neither
profile silently grants root. Application configs and tokens are never installed
or rewritten by profile matching.

## Interpreter-based command

```sh
cd /srv/worker
tired --name worker -- python3 worker.py --queue default
tired --name module-worker -- python3 -m myworker
tired --name java-worker -- java -jar worker.jar
```

The interpreter is the executable; arguments, including relative script/JAR paths,
remain literal. Keep the captured working directory and deploy the application
files separately. A lexical version-switching symlink is preserved and its resolved
identity is shown during review. External application updates may require a fresh
review before a later tired activation.

## Nonroot and root execution

```sh
tired --run-as app --group app --working-directory /srv/app -- /opt/app/app
```

System installation permission is separate from workload identity. A direct root
invocation visibly proposes UID 0 unless you choose another account. To deliberately
select root from a nonroot invocation in automation:

```sh
tired create --yes --allow-risk run-as-root --run-as root -- /opt/admin-tool/tool
```

Additional identified risks require their own acknowledgments. Root/capability
execution through code writable by less-trusted identities is disclosed. Do not
use root as the default response to an application failure.

## User service and lingering

```sh
tired --user ./program
tired status program --user
tired --user --enable-linger --name persistent-user ./program
```

An existing current-user manager/runtime directory is required. Without lingering,
activation can depend on login. `--enable-linger` requests a separately authorized
account-level change and verifies it; it does not guarantee app readiness or home
availability. Removing the service does not disable account lingering. A later
lifecycle command does not replay a past linger request.

## Headless operation

```sh
tired create --yes --json --name relay --profile none --env ENDPOINT=https://example.invalid -- ./relay
tired status relay --json --check-active
tired disable relay --json
tired enable relay --now --json
```

Headless privilege authorization fails promptly unless ordinary administrator
credentials/policy permit the fixed helper. tired installs no permissive policy.
`--yes` does not acknowledge risks. Capture only specifically named variables with
`--pass-env`; use `--import-env-file` for supported snapshot grammar and `--env-file`
for a live reference. Keep secrets out of command arguments.

## Immediate failure

```sh
tired create --yes --json --name failing --profile none -- /opt/app/app --bad-setting
tired status failing
tired doctor failing
tired logs failing --lines 100
```

A failed startup returns 7 with the installation retained. Inspect the actual exit
status and app logs. Correct the config/account/path or edit the service. An explicit
`tired start failing` resets only that failed unit's manager failure/rate-limit
state before activation; it cannot repair an application error.

## Deferred edit

```sh
tired edit relay --apply-mode defer --restart-sec 10s
tired show relay
tired status relay
tired restart relay
```

The new disk configuration is installed, while output identifies the current
process as using an earlier start context. The explicit restart uses the new
configuration. Immediate edits attempt control-plane rollback on startup failure;
application side effects remain outside that rollback.

## Interrupted installation

```sh
tired recover --json
tired recover --transaction TRANSACTION_UUID --resolution rollback --yes --json
```

Use the UUID returned by inspection, not a guessed name. Review pending manager
jobs, owned destinations and backup evidence before choosing rollback or finish.
`tired recover` never automatically replays an uncertain start/restart. If evidence
is incomplete or foreign, preserve it and inspect [recovery details](recovery.md).
