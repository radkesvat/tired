# Native manager reload

`TiredManagerReload` sends the no-argument `org.freedesktop.systemd1.Manager.Reload`
method and accepts only an empty successful reply. It uses the ready identity's
unique bus owner, never a replaceable well-known destination. Service activation
and interactive authorization are disabled. The controller must verify supported
manager version and authorization and persist mutation intent before starting.

The asynchronous operation shares the manager identity's event-loop integration.
Its monotonic deadline is bounded to 1–300000 milliseconds. Polling exposes the
transport descriptor and the earlier of identity and operation deadlines.
Completion, timeout and cancellation release the reply subscription; the borrowed
identity must outlive the operation. Destroy releases handles without undoing a
submitted reload.

`submitted` means the call was queued. `acknowledged` means the verified owner
returned a valid successful reply. Timeout, disconnect or cancellation after
submission cannot prove the reload did not run. A later manager-owner change
invalidates the current result while preserving evidence of an earlier
acknowledgement. A successful reload is not proof of service health or approval of
every installed file.

Access/authentication denial is distinguished from unsupported methods,
unexpected reply types and runtime errors. Remote text is not copied into errors.
The operation does not automatically retry or reconnect: controller recovery must
interpret uncertain manager outcomes with the transaction journal and live state.

The method signature is checked against the supported systemd source's manager
D-Bus documentation. Native broker fixtures verify the exact method/destination,
disabled activation/interactive authorization, successful and malformed replies,
denial, timeout, cancellation and owner invalidation. Tests do not reload the host
manager. Transaction-journal integration and installed-service qualification remain
separate work.
