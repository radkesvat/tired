# Typed job completion events

`tired_job_event_read` decodes `JobRemoved` only at the manager object path, from
the supplied verified unique owner, with signature `uoss`. The numeric ID must
agree with the canonical `/org/freedesktop/systemd1/job/<id>` path. The selected
unit must be a safe full service name. Output owns fixed-size copies and remains
unchanged on failure.

The baseline result set includes `done`, `canceled`, `timeout`, `failed`,
`dependency`, `skipped`, `invalid`, `assert`, `unsupported`, `collected` and `once`.
The additional results beyond the introductory D-Bus documentation are confirmed
in the baseline systemd job-result table. Bounded future lowercase result tokens
are retained with the unknown enum, not coerced into success. Only `done` denotes
successful job execution; it still does not establish the service's current health.

This parser is for signals concerning the selected safe service. A subscription
driver must filter unrelated dependency units, install its signal subscription
before submitting a job, correlate the returned job path, and retain early signals
that arrive before the method reply. It must also track manager-owner validity,
deadlines and cancellation. Decoding alone does not supply those guarantees.

Native message fixtures cover all known outcomes, a future outcome, sender/member/
object/signature mismatch, unsafe names, ID/path mismatch, canonical numeric paths,
overflow and preservation of previous output. No jobs are submitted by these tests.
