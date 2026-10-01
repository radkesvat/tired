# Architecture

The frontend captures a literal invocation, builds a typed proposal, applies
settings and declarative profile advice, and asks for review. Both headless flags
and the ncursesw editor change the same model. Neither rendering nor profile
matching executes a workload or writes a service.

A reviewed mutation binds its semantic digest to the service identity, literal
command, execution account, environment revision, operation, preconditions and
acknowledgments. System operations pass bounded framed IPC to the installed,
root-owned helper through ordinary sudo authorization. User operations use a
bounded worker under the current UID. Neither endpoint accepts filesystem roots
or an alternate manager from frontend data.

Direct frontends embed immutable default profiles and a separately built helper.
They can set up a matching helper cache entry when no matching trusted installed
helper exists. Distribution frontends use only their package-installed helper and
profile files. The helper is built before embedding to avoid recursive payloads;
both executables share a source/configuration identity checked during IPC.

The controller acquires the scope lock, independently validates the complete
request, and derives every destination from the trusted layout and UUIDs. It
prepares immutable candidates and rollback material, records durable intent before
external actions, publishes checked regular files atomically, reloads the selected
manager, changes enablement, waits for native jobs and observes the requested
initial state. A completed service is managed entirely by systemd.

## Modules

| Directory | Responsibility |
|---|---|
| `src/cli` | Parsing, proposal/review orchestration, lifecycle and inspection commands |
| `src/capture` | Passive argv, executable paths, working directory and NSS identity |
| `src/model` | Single typed field registry, semantic constraints and risks |
| `src/profiles` | Strict declarative profiles, matching, evidence and precedence |
| `src/render` | Deterministic systemd token, scalar and environment encoding |
| `src/systemd` | Verified local manager/logind owners, native jobs, properties and journal |
| `src/storage` | Trusted directory descriptors, fingerprints, records, immutable revisions and ledgers |
| `src/transaction` | Digest-bound requests, preflight, commit, rollback and explicit recovery |
| `src/privilege` | Bounded protocol, authorization handoff, helper and record retrieval |
| `src/portable` | Direct-build helper setup, digest verification and atomic cache publication |
| `src/ui` | Review, grouped editors, safe viewers, dashboard and plain fallback |
| `src/util` | Owned buffers, JSON, subprocesses, payload lookup and errors |

Public internal APIs in `include/tired` document ownership and failure behavior.
Owning outputs start zeroed and have destroy functions. JSON is a transport and
inspection format, not a substitute for typed validation. Private service records
omit plaintext environment values; those are hydrated from their checked immutable
revision only when needed.

## Runtime observations

Manager identity, selected scope and baseline version are verified before queries
or mutations. All calls address the pinned owner. A manager restart invalidates
that authority; the operation does not silently follow another owner. Unit files,
loaded configuration, current process state and application health are separate
facts. Short-lived units are temporarily referenced through the transaction's bus
connection so their exit evidence is observable; that reference ends with the
backend connection.

The frontend and helper use monotonic deadlines and bounded output. After an
approved request is transferred, frontend loss cannot abandon the transaction.
The independent worker has a finite deadline and either completes or leaves a
durable recovery record. There is no resident supervisor or daemon.

## Persistent state

System units live in `/etc/systemd/system`; generated environment revisions in
`/etc/tired/services`; private records, history and transactions in `/var/lib/tired`;
the lock lives in `/run/tired`. User equivalents use that account's native unit
location, XDG config/state roots and actual runtime directory. Installation prefixes
and package directories contain immutable payload only.

See [storage layout](storage-layout.md), [service records](service-records.md),
[transactions and recovery](recovery.md), and [security](security.md).
