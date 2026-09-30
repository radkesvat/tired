# Reporting a vulnerability

Report suspected vulnerabilities privately to **asedmosa66@gmail.com**, maintained
by radkesvat. Include the affected version/linkage mode, scope, reproduction,
expected and actual behavior, and the smallest useful redacted example. Do not
send real credentials, private environment files or an unredacted crash dump
without first arranging a secure transfer.

The initial maintained series is 0.1.x. No public release is available until the
maintainer publishes it. Confirm a report against the latest locally prepared
revision when practical; no response or fix deadline is promised here.

Privilege-boundary bypasses, arbitrary privileged file changes, unauthorized
workload identity changes, unsafe transaction recovery and accidental secret
exposure are security-relevant. Application bugs and ordinary startup failures
should include logs with credentials removed. See [the security model](docs/security.md).
