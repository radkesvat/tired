# Journal iteration and follow polling

The stream layer borrows an exclusively used, already scoped journal handle. It
does not open journals or establish whether the caller can access all relevant
files. Apply [selection](journal-selection.md) before starting a stream; retain
the handle until the stream is discarded.

An initial tail accepts zero through 10000 records and returns them in native
journal order. Each step decodes at most one [record](journal-records.md); callers
own the output. The stream retains no messages. A supplied realtime lower bound
is inclusive and filters records from this tail and subsequent follow steps.
Skipped entries are reported separately so an event loop can remain responsive.
Without follow, iteration ends after the initial count. Concurrent rotation and
vacuum can shorten that tail; this is not a snapshot of the journal files.

Zero-line follow anchors to the last existing record without emitting it. An empty
view instead anchors at the head, so the first appended batch is read from its
beginning. After temporary exhaustion, the caller obtains the native descriptor,
event mask, and absolute monotonic deadline, combines them with its terminal and
signal polling, calls process after wakeup, and resumes iteration. The descriptor
belongs to the journal and must not be closed independently. The stream never
sleeps or polls internally. Individual native calls may perform filesystem IO;
there is no hard IO deadline in this layer.

Process distinguishes no change, append, and invalidation. Invalidation can mean
rotation, deletion, or additions elsewhere in the file set. Sequential follow
continues from the native position without replaying earlier history. Callers
should expose invalidation and must not promise lossless collection or silently
reset to the newest tail. UI history refresh is a separate operation.

Reported native seek, read, or watch failures poison the stream. Outputs remain
unchanged on failures, skipped entries, and exhaustion. The caller must discard a
failed stream and its handle instead of retrying past the unread entry.

Native library success does not establish complete access: the baseline library
can ignore unreadable files at open time and drop files after some iteration
errors. Production opening/access diagnostics remain required before an empty
result can be presented as complete. This layer is not yet the logs command.

Fixtures exercise tail ordering, short and empty journals, zero-line follow,
appended batches, inclusive time filtering, rotation notification, and all native
boundary failures. An empty private directory exercises native seek and polling
without writing host journal entries. Real populated and compressed journal
rotation qualification remains outstanding.
