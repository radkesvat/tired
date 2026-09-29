# User lingering observation

The asynchronous lingering query uses a verified login-manager identity on the
local system bus. It calls `GetUser` with the authorized account UID, reads that
object's `UID` property to confirm the account, then reads its boolean `Linger`
property. The returned object path is bounded and validated; the query does not
guess a user-manager or runtime-directory path.

All calls address the pinned unique login-manager owner and disable activation and
interactive authorization. One monotonic deadline covers the full sequence.
Polling can share a frontend event loop. Cancellation detaches the callback slot,
and destruction releases the bus reference and copied path. The identity tracker
must outlive the query and continue receiving ownership changes.

Results separate `known` from `enabled`. A successful false value means lingering
was observed disabled. Missing logind, a missing user object, unsupported
properties, access denial, malformed data, timeout, and owner changes do not mean
disabled. A UID mismatch is a conflict. Partial observations and observations
invalidated by an owner change do not expose a known value.

Lingering lets the user's manager start at boot and remain after logout. Enabled
service files alone do not establish that account-level behavior. Even known
lingering does not prove that a particular workload is healthy or will start
successfully. This query never calls `SetUserLinger` or changes account state.
User-service status displays this observation. Explicitly authorized lingering
enablement remains separate work.

The CLI observation driver queries only the invoking account through the system
bus, without depending on a user runtime-directory path. It uses one two-second
deadline for login-manager discovery and account reads, separately from service
manager observation. A successful observation records its completion time. The
underlying asynchronous API remains available for a terminal event loop; the CLI
driver itself waits synchronously. Filesystem/transport setup is not a hard
real-time operation, and late results are rejected by the deadline checks.

Broker fixtures cover the three-stage protocol, pinned destination and request
flags, true/false, account absence/mismatch/disappearance, malformed replies,
authorization, unsupported properties, timeout, cancellation and owner invalidation.
The native read-only integration check attempts the invoking account when logind
is already available and explicitly reports unavailable observations.
