# Native manager discovery

The asynchronous probe queries `org.freedesktop.systemd1.Manager.Version` through
`org.freedesktop.DBus.Properties.Get`. The caller supplies the intended local sd-bus
connection after checking peer identity; the probe retains its own reference. Requests disable
service activation and interactive authorization. No manager operation or unit
mutation occurs, and no `systemctl` output is parsed.

The reply must contain exactly one string variant. Version text is bounded to 256
bytes and validated before storage. A numbered release prefix is retained separately
from its distribution suffix; versions below 249, unrecognized formats and missing
version properties are unsupported. Unknown values never become version zero with
a successful result. The complete bounded version string remains available for
diagnostics.

Errors distinguish authorization, unavailable service name, missing property,
malformed protocol, disconnect, timeout and other I/O failures. Remote error names
are retained within the same bound; arbitrary remote diagnostic text is not copied
into ordinary error messages. Libsystemd can use NoReply for either timeout or
connection termination, so the probe checks connection state instead of interpreting
the English error message. Version probing does not authenticate the supplied
connection or guarantee that a manager has not restarted afterward.

The caller pumps the probe through `tired_manager_probe_step` and may integrate its
poll descriptor, event mask and absolute monotonic deadline into the frontend event
loop. Each step processes at most sixteen bus events. Deadlines are bounded to
300 seconds and cover connection progress. Cancellation and destruction detach the
callback slot before releasing the bus reference. The supplied connection remains
owned by its caller; all accesses must obey sd-bus thread/lifecycle rules.

Native tests exchange actual sd-bus messages over local socket pairs. They exercise
numbered replies, malformed types and versions, size bounds, remote errors,
timeouts, cancellation, destruction while pending and disconnect. These tests need
no host systemd manager or authorization bypass. The API is an internal backend
interface; no command-line or environment option redirects a privileged connection
to a test peer.

Production bus discovery uses `/run/dbus/system_bus_socket` for system scope and
`$XDG_RUNTIME_DIR/bus` for user scope. It ignores DBUS address overrides and never
guesses another user's session path or creates a missing runtime directory. User
runtime directories must belong to the current UID with no group/other access.
All ancestors must be root/current-user owned and not group/other writable;
symlinks and non-normalized components are rejected. Real and effective IDs must
match. The socket inode must belong to root/system or current UID/user as appropriate.

Connection uses a nonblocking Unix socket through the pinned directory descriptor's
`/proc/self/fd` path. Kernel SO_PEERCRED must identify the expected peer UID before
sd-bus receives the socket. A busy connection queue returns an explicit connection
failure; no unbounded connect wait occurs. Authentication and Hello then progress
asynchronously through the probe/event loop. Peer UID checks authenticate the local
broker identity, not the owner of every well-known name it routes.

Native transport tests complete authentication and probing through a local fixture
broker, reject unsafe directories and endpoints, and show that session address
overrides are ignored. On root test hosts a separate unprivileged listener behind a
root-owned socket verifies that peer credentials are checked independently of the
socket inode's owner.

The manager identity tracker installs its NameOwnerChanged subscription and waits
for broker acknowledgment before querying GetNameOwner. It then queries the UID of
that unique name and requires root for system scope or the current UID for user
scope. All calls share one monotonic discovery deadline. Version queries can target
the verified unique name through `tired_manager_probe_start_unique`, avoiding a new
resolution of the well-known name between checks.

Keep the identity tracker alive and pump its bus after discovery. Any ownership
change during discovery or after readiness invalidates it with a conflict; it does
not silently adopt the new manager. Disconnect also invalidates readiness. Pending
queries and subscriptions are detached on failure or destruction. Poll metadata
includes the initial deadline and continues exposing bus events after readiness.
Future operation admission must check the tracker and use its unique destination;
identity tracking cannot undo a method already executed by a manager.

Native tests verify subscription-before-query ordering, UID rejection, owner changes
during and after discovery, subscription timeout, poll metadata and unique-name
version addressing. These do not yet qualify recovery of real in-flight manager
operations across a systemd restart.

The [unit-query layer](unit-observations.md) provides asynchronous read-only file
state, object lookup and typed Unit/Service observations. The native backend also
drives jobs, enablement, reload, scoped failure reset and controller observation.
Native [load-path discovery](collisions.md) queries UnitPath through the same
verified identity for collision/name-selection orchestration.
The probe is one component of complete controller validation, not standalone write authority.

## Login-manager identity

Login-manager discovery uses the same bounded broker owner/UID verification and
owner-change subscription ordering for `org.freedesktop.login1`. It requires a
peer-validated system bus and a root-owned service name, independently of the UID
of the account being inspected. It neither activates logind nor requests
interactive authorization.

The identity result carries an immutable manager kind. Unit queries, batches,
load-path/name discovery, reloads, jobs, and enablement reject login-manager
identities before issuing unit operations. Login-manager consumers must likewise
require the login kind and address its captured unique name. Ownership changes
invalidate readiness rather than silently switching to a new owner.

This supplies the identity prerequisite for user lingering observation; it does
not read or change the account's Linger property by itself. Missing logind remains
an unavailable observation, not evidence that lingering is disabled.
