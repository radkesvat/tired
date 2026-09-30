# Accounts and generic proposals

Account and group resolution uses reentrant system database lookups with buffers
bounded at 1 MiB. Names and numeric IDs are accepted; malformed selectors and
missing records fail. A user's primary group comes from the account record and
its group lookup, never from assuming the group name equals the username.

Generic proposals retain an independent invoking-account record and copy its
account and primary group into the default service identity. Later explicit identity
selection resolves a new service account without changing that invoking record.
Lookup does not change credentials, authorize installation, or grant capabilities.

For an ordinary invocation, the real UID determines the invoking account. For a
root system-scope invocation with sudo metadata, both `SUDO_UID` and `SUDO_USER`
must be present and resolve to the same account in the system account database.
Incomplete, malformed or inconsistent hints fail with `sudo-origin`. Validated
hints preserve that original account as the default; `--run-as` takes precedence.
These environment values are identity hints, never an authorization mechanism.
Nonroot invocations ignore them, and user scope always uses the current real UID.

A direct root session without sudo hints defaults to root and displays that choice
in review. Selecting root from a nonroot-origin workflow requires the `run-as-root`
acknowledgment. The shared risk review also checks capabilities and privileged code;
the helper independently validates identity, risks and filesystem facts before
mutation. Ordinary frontend invocation can therefore remain unprivileged until the
approved system transaction needs elevation. See [risks](risks.md) and
[security](security.md).

Proposal construction combines capture, passive naming, account resolution, generic
settings, argv, directory, description, logging identifier, retry defaults, and scope
into a model. It replaces outputs only on success and performs no manager mutation
or target execution. Names remain tentative until collision checks and commit-time
reservation. Account records can change after capture and require revalidation.
