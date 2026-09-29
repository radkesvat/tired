# Historical profile provenance

The schema-1 profile snapshot stores the complete validated profile document,
original source path/digest/origin, explicit-selection flag and one recorded
disposition per recommendation in document order. Profile ID, revision, reasons,
conditions and source references remain in the embedded document. Loading never
reapplies recommendations or changes the service model.

The source SHA-256 describes the bytes read during trusted profile discovery.
Reserializing the embedded JSON may change formatting, so its digest need not equal
that source digest. This codec checks digest format and profile structure, not the
current source file or its authenticity. A containing service record must enforce
scope/source trust and consistency with the saved model and approval evidence.

Snapshots require a selected profile; a service with no selected profile must not
invent one merely to populate provenance. Input/output are bounded to 1 MiB and
the embedded document retains the existing 256 KiB profile limit. Source paths are
bounded absolute strings, not instructions to read a file while decoding.

Parsing rejects unknown fields, invalid source origins/digests, invalid profile
documents, unknown decisions and a decision count that differs from recommendation
count. Parsed snapshots own their profile document and strings. Encode views may
borrow an already validated profile. Failures preserve prior owned output.

Native tests use a bundled profile to verify document identity/revision, source
metadata, recommendation decisions, stable re-encoding, invalid digests/counts/
decisions and output preservation.
