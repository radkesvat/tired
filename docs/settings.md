# Settings model

The settings parser accepts a JSON object with `schema_version: 1`. Unknown keys,
duplicate keys, unsupported versions, invalid types and out-of-range values fail
without changing the destination. Input is bounded to 1 MiB. The installed
`settings.schema.json` describes the structure; C validation also enforces duration,
path, memory-budget and authority constraints.

| Key | Default | Accepted values |
| --- | --- | --- |
| `color` | `auto` | `auto`, `always`, `never` |
| `ascii` | `false` | Boolean |
| `tui` | `true` | Boolean |
| `retry_policy` | `persistent` | `persistent`, `limited` |
| `restart_sec` | `5s` | Duration from zero through one day |
| `history_revisions` | `32` | Integer 1–1000 |
| `log_tail` | `200` | Integer 1–10000 |
| `observation_sec` | `3s` | Duration from 100 ms through 300 s |
| `profile_directories` | Empty | Up to 16 absolute paths, 64 KiB combined |

Durations use the typed model's exact microsecond parser: `us`, `ms`, `s`, `min`,
`h`, or `d`, with at most six decimal places; no suffix means seconds. Persistent
retries require a nonzero delay.

Parsed files are partial layers. Omitted fields do not overwrite preceding values.
Merge administrator settings into initialized defaults, then user settings into the
result. Every supplied field records its origin. A failed merge preserves the entire
previous result, including owned directory strings. Cross-field constraints are
checked against the effective merged values: a zero delay can inherit a preceding
limited retry policy, but cannot accompany effective persistent retries.

Only administrator layers may supply `profile_directories`. User layers cannot
supply that key even with an empty list. This parser does not establish file trust
or authorize directories; callers must enforce discovery and filesystem trust rules.
No keys can change privileged state paths or bypass validation.

Owning `TiredSettings` objects must be initialized to zero before first use and
destroyed when finished. Parsed layers must be merged before use. Parsing and
merging perform no filesystem or manager operations.

File discovery, CLI application, and `config show`/`config validate` integration
remain under implementation. The executable does not yet consume these settings.
