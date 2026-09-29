# Historical review snapshots

The schema-1 review snapshot records the approved-request SHA-256 digest, argument
count, acknowledged risk codes and sensitive argument indices. It contains no
argument values. Encoding writes risk codes in registry order and indices in
ascending order; parsing accepts any order but rejects duplicates.

Only known risk identifiers are accepted. Argument zero cannot be marked sensitive,
and every index must be below the recorded argument count. Native parsing enforces
these cross-field limits in addition to the installed schema, rejects extra fields,
and preserves previous output on failure. Input/output are bounded to 1 MiB.

Loading this snapshot does not approve an operation. The controller must verify
the digest against the complete approved request and check that the argument count
matches its model. It must rerun current risk and commit-time precondition checks;
historical acknowledgments cannot waive unknown or changed conditions. Profiles
cannot create approval simply by supplying a snapshot.

Tests cover canonical round trips, individual acknowledgments and markings,
duplicate/unknown codes, duplicate/out-of-range indices, forbidden index zero,
maximum argument indices and preservation on parse/encode failure.
