# Applying a manifest file step

`tired_file_change_apply` dispatches one entry from a validated file manifest. It
checks the selected scope and held operation lock and resolves the destination
through the trusted layout. Callers cannot supply another path or permission mode.
Directories must already exist. This API neither creates them nor cleans artifacts.

Forward creation reopens staging and publishes without replacement. Forward edit
exchanges the recorded before/after inodes. Forward removal moves the before inode
to a retained name derived from its rollback UUID. Retries use the same stored
identities and operation-specific primitive checks.

Reverse replacement exchanges the retained original back. Reverse removal restores
the retained inode without replacing a destination. Reverse creation moves the
new inode aside using the staging UUID with the `.removed` suffix; if the active
name is already absent, it syncs and rechecks absence. Staged or retired new bytes
remain for separate cleanup. Foreign active files are never accepted as the
recorded creation when reversing it.

The result resets for each call and carries progress even on failure. `reached`
means the primitive reached or recognized the selected direction's resulting name
state; it does not certify that later checks succeeded. `durable` requires the
step's synchronization and validation. False never proves an earlier attempt made
no changes. Handles are closed without discarding any transaction-owned artifact.

This is a file-step executor, not transaction authorization or the full controller.
The caller must bind the manifest to its journal and approved request, establish
managed ownership, prepare backups/directories, persist intent, choose ordering,
and record the outcome. Manager changes and service-level rollback remain separate.

Native integration fixtures apply and reverse creation, replacement and removal
for unit and record targets. They cover repeated calls, rollback before creation,
foreign recreation and invalid indices. The underlying primitive tests inject sync
failures and source changes.
