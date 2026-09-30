# Service workflows

[Walkthroughs](../docs/walkthroughs.md) cover generic binaries, Backhaul, FRP,
interpreters, account choices, user lingering, headless operation, initial failure,
deferred edits and interrupted changes.

The populated review shows the literal command, captured working directory,
workload account, runtime/boot choices and recommendation origins. Press `P` for a
unit preview, `W` for profile evidence, `R` for risks and `C` to apply the reviewed
model. Advanced pages use the same typed fields as `--set`. `--no-tui` provides plain
review. Running tired without operands opens the [managed-service dashboard](../docs/list.md)
when TUI is enabled and the terminal is usable. Setting `tui` to `false` or using
`--no-tui` selects compact listing; `--json` selects JSON listing.

`?` opens the selected field's rationale, disposition and cited sources. `H` shows
the opt-in baseline hardening preset; Space selects its displayed settings and
Escape returns. The same preset is available as `--hardening baseline` in headless
plans and creation. Review compatibility warnings and application write needs.
Editors preserve pending input while showing a resize message and block edits
until enough screen space is available.

Use `status` for current state, `show --effective` for file/drop-in evidence, `logs`
for the selected journal and `doctor` for scope/service diagnostics. A running
process is not a guarantee of application health. Start and enablement are
independent; stopping a networking service can disconnect an SSH route.

A failed active edit attempts to restore its previous control-plane revision.
Deferred edits clearly identify the older running context. An incomplete
transaction requires an explicit recovery decision and never silently overwrites
foreign files or replays an uncertain start.
