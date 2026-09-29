# Log query options

The command parser accepts `logs NAME` with `--user` or `--system`, `--json`,
`--follow`, `--lines N`, `--since TIMESTAMP`, and `--boot current|BOOT_ID`.
These query options are rejected on other frontend commands. Arguments after a
workload executable retain their ordinary workload meaning.

`--lines` accepts 0 through 10000. Zero requests no existing records and is useful
with follow. An omitted line count stays unset so execution can apply the user's
configured log-tail size. Duplicate scalar options and duplicate follow flags
are errors.

`--since` is an inclusive realtime bound. Accepted timestamp forms are:

- `@SECONDS` or `@SECONDS.ffffff`, nonnegative Unix seconds with up to six
  fractional digits, for example `@1700000000.25`.
- `YYYY-MM-DDTHH:MM:SSZ` with optional one-to-six fractional second digits before
  `Z`, for example `2026-09-29T12:30:00.123456Z`.

UTC calendar input starts at 1970 and validates month lengths and Gregorian leap
years. Leap-second values, local times, numeric offsets, relative times, and
ambiguous date-only strings are rejected. Parsing does not read the clock, consult
locale/timezone settings, or normalize invalid dates. Overflow is rejected before
converting to unsigned microseconds.

`--boot current` is retained as a request to resolve the host boot ID at execution
time. Explicit IDs contain exactly 32 hex digits; uppercase digits are normalized
to lowercase. Omitting boot selection leaves all accessible boots eligible.

These are validated request fields, not a claim that the logs command is already
operational. Production journal access and frontend integration remain pending.
