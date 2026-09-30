# Complete field reference

These are the shared CLI/TUI registry fields. Use `--set FIELD=VALUE` or
`--unset FIELD`; selected fields still require semantic and live validation.
Lists accept one literal member per repeated assignment. Registry defaults
precede configured/profile/user choices; capture supplies identity, executable,
arguments, directory and name. See [model rules](model.md) for exact grammar,
bounds, retry resolution, scope and field combinations.

| Field | Type | Directive | Registry default | Choices |
|---|---|---|---|---|
| `name` | text | control-plane choice | inherit/capture | — |
| `description` | text | Description | inherit/capture | — |
| `scope` | choice | control-plane choice | system | system\|user |
| `run_as` | text | User | inherit/capture | — |
| `group` | text | Group | inherit/capture | — |
| `executable` | text | ExecStart | inherit/capture | — |
| `working_directory` | text | WorkingDirectory | inherit/capture | — |
| `type` | choice | Type | exec | exec\|simple\|notify\|forking\|oneshot |
| `pid_file` | text | PIDFile | inherit/capture | — |
| `remain_after_exit` | boolean | RemainAfterExit | inherit/capture | — |
| `restart` | choice | Restart | on-failure | no\|on-failure\|always\|on-abnormal\|on-success\|on-abort\|on-watchdog |
| `restart_sec` | duration | RestartSec | 5s | — |
| `retry_policy` | choice | control-plane choice | persistent | persistent\|limited |
| `start_limit_interval` | duration | StartLimitIntervalSec | 0 | — |
| `start_limit_burst` | integer | StartLimitBurst | inherit/capture | — |
| `timeout_start` | timeout | TimeoutStartSec | inherit/capture | — |
| `timeout_stop` | timeout | TimeoutStopSec | 30s | — |
| `kill_mode` | choice | KillMode | control-group | control-group\|mixed\|process\|none |
| `standard_input` | choice | StandardInput | null | null |
| `standard_output` | choice | StandardOutput | journal | journal\|null |
| `standard_error` | choice | StandardError | journal | journal\|null |
| `syslog_identifier` | text | SyslogIdentifier | inherit/capture | — |
| `nice` | integer | Nice | inherit/capture | — |
| `no_new_privileges` | boolean | NoNewPrivileges | inherit/capture | — |
| `private_tmp` | boolean | PrivateTmp | inherit/capture | — |
| `protect_system` | choice | ProtectSystem | inherit/capture | false\|true\|full\|strict |
| `protect_home` | choice | ProtectHome | inherit/capture | false\|true\|read-only\|tmpfs |
| `network` | choice | control-plane choice | none | none\|network\|online |
| `start` | boolean | control-plane choice | true | — |
| `enable` | boolean | control-plane choice | true | — |
| `enable_linger` | boolean | control-plane choice | false | — |
| `argv` | list | ExecStart | inherit/capture | — |
| `supplementary_groups` | list | SupplementaryGroups | inherit/capture | — |
| `environment_files` | list | EnvironmentFile | inherit/capture | — |
| `after` | list | After | inherit/capture | — |
| `wants` | list | Wants | inherit/capture | — |
| `requires` | list | Requires | inherit/capture | — |
| `requires_mounts_for` | list | RequiresMountsFor | inherit/capture | — |
| `read_write_paths` | list | ReadWritePaths | inherit/capture | — |
| `runtime_directory` | list | RuntimeDirectory | inherit/capture | — |
| `state_directory` | list | StateDirectory | inherit/capture | — |
| `nofile.soft` | limit | LimitNOFILE | inherit/capture | — |
| `nofile.hard` | limit | LimitNOFILE | inherit/capture | — |
| `memory_max` | limit | MemoryMax | inherit/capture | — |
| `tasks_max` | limit | TasksMax | inherit/capture | — |
| `cpu_quota` | percentage | CPUQuota | inherit/capture | — |
| `umask` | octal mode | UMask | inherit/capture | — |
| `runtime_directory_mode` | octal mode | RuntimeDirectoryMode | inherit/capture | — |
| `state_directory_mode` | octal mode | StateDirectoryMode | inherit/capture | — |
| `kill_signal` | signal | KillSignal | SIGTERM | — |
| `success_exit_status` | list | SuccessExitStatus | inherit/capture | — |
| `restart_prevent_exit_status` | list | RestartPreventExitStatus | inherit/capture | — |
| `capability_bounding_set` | list | CapabilityBoundingSet | inherit/capture | — |
| `ambient_capabilities` | list | AmbientCapabilities | inherit/capture | — |
| `wanted_by` | choice | WantedBy | inherit/capture | multi-user.target\|default.target |
