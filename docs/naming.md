# Service names

Automatic names use the executable basename, lowercased in ASCII. Letters, digits,
periods, underscores, and hyphens are retained. Runs of unsupported bytes become
hyphens; leading and trailing punctuation is removed. Adjacent dots are broken up
to avoid traversal-like names. An empty result becomes `service`. Automatic bases
are capped at 80 bytes before suffixing.

The following passive hints select a more useful base without modifying arguments:

| Invocation | Base |
| --- | --- |
| `python3 bot.py` | `bot` |
| `python3 -m package.worker` | `package-worker` |
| `node server.js` | `server` |
| `bash worker.sh` | `worker` |
| `java -jar relay.jar` | `relay` |

Python, Python 2/3, and versioned Python 2/3 interpreter names are recognized.
Script hints remove the final filename extension. Only these direct invocation
forms are interpreted; arbitrary interpreter options and wrapper chains fall back
to the executable name. `env`, `sudo`, `nohup`, `tmux`, and shell command wrappers
are marked for a warning. They are not stripped. A virtual-environment interpreter
is still the command that will execute.

Explicit names preserve case and must contain 1–200 ASCII bytes, start and end
with a letter or digit, and otherwise use letters, digits, `.`, `_`, or `-`.
Adjacent dots, paths, template markers, controls, and other characters are errors.
An optional `.service` suffix is removed once when parsing an explicit name.

Candidate generation adds `.service` to a base, or `-2.service`, `-3.service`, and
so on. It does not establish availability or ownership. Manager collision checks,
transaction locks, and commit-time revalidation are separate required operations;
this naming module alone must never authorize installation or replacement.
