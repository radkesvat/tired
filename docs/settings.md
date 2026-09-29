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

The storage API `tired_settings_load` accepts explicit administrator and user paths,
loads them in that order over built-in defaults, and publishes the result only after
both layers succeed. A null path skips a layer; a missing file or directory means
that optional layer is absent. Other open, read, parse, trust, or merge failures
preserve the caller's previous settings and report an error.

Trusted loading requires absolute paths with normalized components. It traverses
from the filesystem root using directory descriptors and refuses symlinks. Every
ancestor and the file must be owned by root or the relevant user and must not be
group- or other-writable; administrator paths permit only root ownership. The final
file must be regular with one hard link. Reads are bounded, detect observed changes
and recheck file trust afterward. These checks do not defend against an already
trusted owner changing their own files. This API takes paths from its caller and
does not read environment variables or select discovery locations.

Owning `TiredSettings` objects must be initialized to zero before first use and
destroyed when finished. Parsed layers must be merged before use. Parsing and
merging perform no filesystem or manager operations.

`tired config validate FILE [--user] [--json]` reads the explicit file and validates
it over built-in defaults without loading installed settings or contacting a manager.
`--user` additionally enforces user-layer restrictions. Validation does not check
file trust or the existence or trust of profile directories. JSON output identifies
the validation base and this trust limitation. A partial file that relies on another
layer's retry policy must include that policy to pass standalone validation.

`tired config show [--json]` loads `/etc/tired/config.json`, followed by the invoking
user's `$XDG_CONFIG_HOME/tired/config.json`. When XDG_CONFIG_HOME is empty or unset,
it uses `.config/tired/config.json` beneath the account database home directory.
A relative XDG_CONFIG_HOME is an error. Discovery uses the trusted storage checks
above. It requires matching real and effective IDs and does not infer another user
from sudo environment variables. Both scopes use the invoking user's preferences;
user settings never gain administrator authority.

Explicit `--color` and `--no-tui` choices apply last. Text and JSON show every
effective field and its `default`, `administrator`, `user`, or `cli` origin.
Durations are shown as exact microsecond strings. Neither format contacts systemd.
Malformed or unsafe installed settings fail the command without partial output.

Offline plans consume configured retry policy and restart delay before profile
recommendations and CLI selections. Dependent start-limit defaults are recomputed
from the resulting policy. Explicit inheritance is preserved like other user
choices. The low-level generic planning API remains independent of discovery;
the settings-aware API accepts an already merged configuration.

Approved profile directories and the presentation, history, log-tail and observation
settings still need integration into their respective operations. Config show
reports their effective defaults without claiming those consumers are complete.
