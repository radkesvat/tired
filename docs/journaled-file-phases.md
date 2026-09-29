# Durable file-phase execution

`tired_transaction_files_apply` binds private `files.json` and `journal/` to the
trusted approved preparation anchor supplied by the controller. It checks scope,
forward/rollback mode, pending action, unexplained journal staging and capacity for
the required outcome record before entering the file phase.

The file action is `remove_files` for removal, `publish_files` for configuration
creation/edit/rename/restore, and `store_record` for lifecycle record changes.
A fresh phase appends and syncs intent before any destination mutation. A matching
pending phase resumes without appending another intent. Resumption syncs the journal
directory because a previous publication could have renamed its already-synced
record but failed the directory sync.

Ordered file execution then records `completed` on success or `uncertain` on any
file failure. Uncertainty remains pending, permitting a later explicit retry.
The runner never automatically switches to rollback. Inverse execution requires
the journal already to be in rollback mode with no incompatible pending action.

Results distinguish durable intent, file completion, durable outcome, failed-step
progress and file errors. If outcome recording fails, the returned error describes
that journal failure while `file_error` preserves any file failure. Retained journal
staging is left for recovery inspection rather than silently discarded. Repeated
calls after a completed phase start another recorded phase; the controller must
choose whether a phase is actually required.

This component does not establish user authorization, validate an entire request,
prepare backups, enforce manager/file phase ordering, or commit the transaction.
Those remain controller responsibilities. The approved anchor must come from that
trusted controller, not unvalidated helper input.

Native integration tests reject an incompatible rollback mode, record uncertainty
after partial creation, resume to completion with the same intent, and execute an
inverse file phase after an explicit rollback marker.
