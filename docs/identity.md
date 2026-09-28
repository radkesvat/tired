# Accounts and generic proposals

Account and group resolution uses reentrant system database lookups with buffers
bounded at 1 MiB. Names and numeric IDs are accepted; malformed selectors and
missing records fail. A user's primary group comes from the account record and
its group lookup, never from assuming the group name equals the username.

Generic proposals use the real UID recorded during command capture. They retain
an independent owned invoking-account record and copy default service identity
fields into the model. Later explicit identity selection must resolve a new service
account without changing that invoking record. Lookup does not change credentials,
authorize installation, or grant capabilities.

No SUDO_* environment variable is currently used to infer authority or identity.
When launched through sudo, the generic proposal therefore reflects the captured
root identity. Prefer invoking the frontend as the ordinary account and elevating
only the approved transaction once that workflow is available. The review UI must
show root execution explicitly; identity risk review remains to be implemented.

Proposal construction combines capture, passive naming, account resolution, generic
settings, argv, directory, description, logging identifier, retry defaults, and scope
into a model. It replaces outputs only on success and performs no manager mutation
or target execution. Names remain tentative until collision checks and commit-time
reservation. Account records can change after capture and require revalidation.
