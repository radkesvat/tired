# Native manager discovery

The asynchronous probe queries `org.freedesktop.systemd1.Manager.Version` through
`org.freedesktop.DBus.Properties.Get`. The caller supplies the intended authenticated
local sd-bus connection; the probe retains its own reference. Requests disable
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

System/user socket discovery and authentication, manager identity/restart tracking,
unit queries, jobs, mutation methods and frontend wiring remain under implementation.
This probe alone is not a completed live validation or service-management backend.
