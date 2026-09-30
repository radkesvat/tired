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

Bounded file I/O, immutable private revisions, CLI integration, redacted JSON and
protected export use these codecs. Persisted records omit plaintext values and
hydrate them from checked private revisions when needed. The baseline
[execution documentation](https://raw.githubusercontent.com/systemd/systemd/v249/man/systemd.exec.xml)
describes environment-file ordering and credential facilities.

## Supported import grammar

The in-memory importer accepts UTF-8 assignment files up to 2 MiB, with a decoded
collection still bounded by 1 MiB. This allows escaping to expand an encoded file
without shrinking the usable environment budget. Reject NUL, invalid variable names,
missing `=`, unclosed quotes, and dangling backslashes. A failed import changes no
existing assignment, including assignments parsed before the failure.

Blank lines and lines beginning with `#` or `;` after whitespace are comments.
A backslash in a comment escapes the next character, including a newline. Horizontal
space around names and before values is ignored. Unquoted values preserve internal
space and trim unescaped trailing space. Backslash quotes the next character;
backslash-newline continues the value without a newline.

A value beginning with a single or double quote must end with the matching quote,
followed only by horizontal space and newline/EOF. Quoted values may span lines.
Single quotes preserve all enclosed bytes. In double quotes, backslash removes the
special meaning of quote, backslash, dollar, or backtick; backslash-newline is removed.
Other backslashes remain literal. Quote concatenation is not supported. Shell
`export` prefixes are rejected. No variable or command substitution occurs.

The encoder sorts names bytewise and always double-quotes values, preserving literal
newlines and escaping quote, backslash, dollar, and backtick. It returns private
contents including sensitive values, never a display view. The format was checked
against the [baseline parser behavior](https://github.com/systemd/systemd/blob/v249/src/basic/env-file.c).
Native encoder/import round trips and real-manager duplicate live/managed value
precedence are exercised. See [security](security.md) for persistence and export.
