# Environment and credential records

Environment assignments use ASCII names matching `[A-Za-z_][A-Za-z0-9_]*`, capped
at 255 bytes. Values preserve valid UTF-8, empty strings, equals signs, controls,
and literal shell metacharacters; NUL is rejected. Nothing is shell-expanded.
Each environment contains at most 1024 variables and 1 MiB including name/value
terminators. Failed updates leave the prior collection intact.

Precedence is defaults, profile, configuration, imported files, selected exported
variables, explicit assignments, then final editor changes. Higher-precedence values
win regardless of insertion order. Within a level the last assignment wins.
`pass` reads only the explicitly selected name from the process environment and
fails if absent. It does not iterate or import the whole environment.

Every entry records its origin and sensitivity. Names containing TOKEN, SECRET,
PASSWORD, PASSWD, API_KEY, PRIVATE_KEY, CREDENTIAL, ACCESS_KEY, or AUTH are classified
case-insensitively. Callers may explicitly mark other values sensitive. Classification
is retained when replacing a value. The default display accessor returns a redaction
marker for sensitive entries; raw values are for private serialization only.
This heuristic can miss secrets and does not sanitize terminal control characters;
output encoders must escape controls independently. No secure-memory or encryption
claim is made.

Credential records accept `NAME=/absolute/path`, with ASCII letters, digits,
underscores, and hyphens in the name. Duplicate names fail. They store references
only, never read contents, and never alter command arguments. At most 256 references
and 1 MiB are retained. Source permissions, application compatibility, host credential
support, and secure rendering must be validated later.

Environment-file import, owned private-file encoding/storage, CLI integration,
redacted JSON, and private export remain under implementation. The records alone
must not be presented as completed secret handling. The baseline
[execution documentation](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.exec.xml)
describes environment-file ordering and credential facilities.
