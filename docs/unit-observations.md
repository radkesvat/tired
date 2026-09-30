# Typed unit observations

The observation decoder consumes an unread `a{sv}` body from a Properties.GetAll
reply for the Unit or Service interface. It does not issue a request, verify the
sender or claim that a unit is managed by tired. The backend must establish those
properties before passing a reply to it.

Every field has a separate `known` flag. A missing MainPID differs from a reported
zero PID; an absent exit status differs from zero; an absent fragment path differs
from a reported empty path. Unknown states remain unknown rather than receiving
default values that could imply success.

| Interface | Fields and wire types |
| --- | --- |
| Unit | Id, LoadState, ActiveState, SubState, UnitFileState, FragmentPath: strings |
| Unit | DropInPaths: ordered array of strings |
| Unit | ActiveEnter/ExitTimestamp and InactiveEnter/ExitTimestamp, including each Monotonic counterpart: unsigned 64-bit |
| Service | Result: string; MainPID and NRestarts: unsigned 32-bit |
| Service | ExecMainCode and ExecMainStatus: signed 32-bit |
| Service | ExecMainStart/ExitTimestamp and Monotonic counterparts: unsigned 64-bit |

These types follow the baseline manager interfaces. Timestamps retain native
microseconds and their wall-clock/monotonic distinction. State strings are retained
without assuming that the vocabulary will never grow.

Drop-in paths retain manager order and are copied into owned storage. A known empty
array differs from an absent property. The decoder accepts at most 256 absolute
paths, each at most 4096 bytes, with a 256 KiB combined budget including terminators.
Invalid text, relative paths, wrong wire types and excessive lists fail atomically.
An update to Service properties deep-copies the existing Unit path array; partial
decode/copy failures release all newly owned items. Paths are observations, not
permission to read, edit or delete those files. They do not prove disk contents or
effective directive values.

Decoding replaces the selected interface's fields as a group, clearing properties
absent from the new response. It preserves the other interface's existing values.
Failure preserves the complete previous observation. This atomic memory update
does not imply that separately queried Unit and Service properties describe one
atomic instant in the manager.

The decoder accepts at most 512 dictionary entries and 128 KiB of property names.
Names must be valid D-Bus member names and unique, including unknown properties.
Known fields require their exact wire type and correct interface. Strings are
bounded to 256 bytes, except FragmentPath at 4096, and reject invalid UTF-8/control
text. Unknown properties are skipped for forward compatibility; their wire decoding
remains subject to libsystemd's D-Bus message limits. Owned destinations start zeroed
and must be destroyed after use.

Native message fixtures exercise known-zero versus absent values, exact integer
width/sign, large timestamps, unknown variants, duplicate keys, type errors,
property-count limits, preservation on failure and clearing stale fields.

The default asynchronous query performs GetUnitFileState, GetUnit and (when an object
exists) Unit and Service GetAll. It targets the verified unique manager name and
checks identity readiness at each stage. One deadline covers the entire sequence;
late replies, cancellation, access errors, disappearing objects or malformed
properties produce a failed result without publishing partial observations.

File-state availability and manager-object existence are separate. A manager may
report an installed unit's file state while GetUnit reports no loaded object; its
runtime fields then remain unknown. Conversely, generated or transient units may
have state without an ordinary persistent file. `file_found` means GetUnitFileState
returned a state, not that tired proved a regular file exists. The default query does not
call LoadUnit or reserve a name. Missing manager entries are not sufficient proof
that a filesystem pathname is safe to overwrite.

Explicit configuration inspection can instead call `tired_unit_query_start_lookup`
with `TIRED_UNIT_LOAD_CONFIGURATION`. This replaces GetUnit with LoadUnit so an
installed but unloaded unit can expose its configuration. LoadUnit may create an
in-memory manager unit object; it does not start/stop a workload, enqueue a job,
enable a unit or reload an already loaded configuration. This distinction follows
the baseline manager interface. The request still targets the pinned unique owner,
disables bus activation and interactive authorization, and shares one deadline with
the subsequent property reads.

`configuration_load_queued` records asynchronous submission; it does not prove
delivery. `configuration_load_acknowledged` records a valid object-path reply. These
historical flags survive a later property failure or cancellation, while failed
queries continue to hide partial observations. Cancellation cannot undo manager
configuration loading. A missing-unit reply retains ordinary known absence; a
denial, timeout or malformed reply retains its error. Broker fixtures cover those
cases, inactive loaded configuration, and cancellation after submission. Existing
status/list/recovery callers keep the default loaded-only lookup. `show --effective`
uses the explicit configuration lookup through the shared CLI inspection driver.

The identity tracker must outlive its queries. Destroy/cancel detaches pending
callbacks. Results borrow query storage and are available only after successful
completion; a subsequently observed manager identity change hides a completed
snapshot and returns the identity error. These sequential reads are not a
transactional manager snapshot and make no application-health claim.

Native broker tests cover loaded, unloaded and absent cases, exact destinations
and flags, malformed replies, authorization, disappearance, late responses,
cancellation and completed-snapshot invalidation. A read-only host integration test
discovers the system manager and observes systemd-journald; it explicitly skips if
the system bus/manager is unavailable or inaccessible. Status/doctor/frontend
presentation is provided by status, list and the dashboard. The [collision checker](collisions.md) can combine
this query result with supplied load locations and pending reservations; discovery
and commit-time integration use the native backend and transaction controller.

## Observing multiple units

`TiredUnitBatch` copies up to 3072 safe full service names and queries them in
input order through the same authenticated manager identity. Duplicates remain
distinct observations. One monotonic deadline covers the entire batch; it is not
reset for every unit. Each step pumps bounded bus work and admits at most one
new unit query. Poll integration and cancellation are available.

Results distinguish batch failure from per-unit failure. A denied or malformed
unit response remains an error for that item while other units can still be
observed. Deadline, cancellation or loss of manager identity stops new queries.
Unattempted items are marked explicitly and receive the batch error, never an
invented absent/stopped state. Callers must check each item's completion and error
before interpreting presence or property values.

Completed queries have a realtime microsecond completion timestamp, including
error replies. Zero means no completed observation. Successful results remain
borrowed from the batch until destruction. The manager identity must outlive the
batch; owner invalidation hides successful snapshots even after completion.
Sequential observations are not an atomic view of all units, and wall-clock
timestamps may jump; monotonic time alone governs deadlines.

Broker fixtures cover successful/denied batches, cancellation, a deadline shared
across several delayed queries, empty batches and post-completion invalidation.
Live integration reads the existing journald unit through this API without loading
or mutating units. Recovery inspection collects these observations without granting mutation authority.
