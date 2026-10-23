# Terminal-Only Copilot Activity Indicators

## Decision

Implement the first version entirely in Windows Terminal. GitHub Copilot CLI
and Copilot Agent Runtime require no changes.

This changes the feature contract from exact Copilot session status to a
pane-level activity heuristic:

- `working`: a detected Copilot process in the pane is reporting terminal
  progress.
- `attention`: Copilot stopped reporting progress, exited after reporting
  progress, or raised a bell/notification while the pane was not focused.
- `none`: no unacknowledged Copilot activity is known.

Terminal will not claim to know whether attention means permission, a question,
successful completion, or failure. It will not count or identify Copilot's
foreground and background sessions.

The richer per-session OSC protocol in `architecture.md` remains a possible
future enhancement, but it is not a dependency of this plan.

## User Experience

Each tab gets one dedicated Copilot status slot in its header:

| Tab state | Visual meaning |
|---|---|
| Any pane needs attention | Copilot may need the user in at least one pane |
| Otherwise, any pane is working | Copilot is working in at least one pane |
| Otherwise | No Copilot indicator |

The attention state has higher priority than working. If one pane needs
attention while another pane is working, the tab shows attention.

The indicator tooltip uses deliberately conservative language:

- `Copilot is working in 2 panes`
- `Copilot may need attention in 1 pane`

Focusing the owning pane acknowledges and clears its attention latch. Merely
selecting a tab does not clear attention in another pane in that tab.

The indicator is separate from the existing taskbar progress ring and bell
icon. Existing progress and bell behavior remains unchanged.

## Supported Scope

Version 1 supports:

- Local Windows ConPTY connections.
- The shipped Copilot single-executable application, `copilot.exe`.
- Copilot launched directly as the pane's root process.
- Copilot launched by PowerShell, Command Prompt, or another native Windows
  wrapper when `copilot.exe` remains a descendant of the pane's root process.
- Multiple panes containing Copilot in one tab.
- Multiple tabs and windows.

Version 1 does not support:

- Copilot running inside WSL, SSH, Azure Cloud Shell, or another remote
  connection. Windows Terminal cannot inspect the remote process tree.
- Development launches through `node.exe` and `copilot-dev` without an
  additional opt-in executable classifier.
- A wrapper that detaches Copilot from the pane's native process tree.
- Exact status for Copilot background sessions inside one `copilot.exe`
  process.
- Permission-versus-question or success-versus-failure classification.
- Inferring state by scraping rendered terminal text, prompts, spinner glyphs,
  localized messages, or command history.

## Existing Signals

### Process identity

The released Windows Copilot CLI is built as `copilot.exe`. Terminal already
owns the local ConPTY connection and retains the root client process handle in
`ConptyConnection`.

Terminal can therefore associate a native process tree with a pane:

1. Obtain the pane's root process ID from its local `ConptyConnection`.
2. Take one application-wide process snapshot.
3. Build a parent-to-children index.
4. Walk descendants of each registered pane root.
5. Classify the pane as Copilot-owned when a live descendant executable is
   named `copilot.exe`, case-insensitively.

Walking descendants, rather than inspecting only the root process, supports
normal shell and wrapper launches.

Process identity is only used to qualify pane-local status. No command line,
prompt text, working directory, environment block, or Copilot session
identifier is collected or displayed.

### Existing OSC 9;4 progress

Copilot CLI already emits OSC `9;4` indeterminate taskbar progress while its
foreground agent is thinking. It refreshes the state periodically and clears
it when foreground thinking stops.

Windows Terminal already parses this signal and stores it per control as
taskbar state. The Copilot feature observes the same state transitions without
changing OSC parsing:

- Copilot process present plus taskbar state `3` means `working`.
- Transition away from taskbar state `3` while Copilot is still present can
  create `attention`.

OSC `9;4` is generic. Correlation with a live `copilot.exe` descendant is what
allows Terminal to attribute the progress to a Copilot-owned pane. A child
application launched by Copilot may also emit progress; treating that as part
of Copilot's work is acceptable for this heuristic.

### Bell and notification

Copilot can optionally emit BEL or a terminal notification when attention is
needed. These features are configuration-dependent and cannot be the primary
signal.

When a bell or terminal notification arrives from a pane currently classified
as Copilot-owned, Terminal latches generic Copilot attention in addition to
preserving the existing bell or notification behavior.

## Architecture

### Application-wide process snapshot service

Add a small Windows-only service in TerminalApp that snapshots native processes
once per interval for all eligible panes. Do not create one process scan per
pane.

The service stores only the information required for the current snapshot:

- Process ID.
- Parent process ID.
- Executable basename when accessible.
- Process creation time when accessible, to reduce PID-reuse ambiguity.

The service exposes a result keyed by registered pane identity:

```text
PaneProcessState
  root process identity
  copilot process identities
  sampled timestamp
```

Use `CreateToolhelp32Snapshot` or the repository's existing equivalent if one
is available during implementation. A single snapshot is converted to an
adjacency map, then each pane root is traversed with cycle and depth guards.

Sampling rules:

- Start the timer when the first eligible local ConPTY pane registers.
- Stop it when the last eligible pane unregisters.
- Sample every two seconds while eligible panes exist.
- Trigger an immediate sample when a pane registers, restarts, or its
  connection changes.
- Perform snapshot work off the UI thread.
- Marshal only changed pane classifications back to the UI thread.

The two-second interval bounds typical indicator latency without polling once
per pane. Performance telemetry should measure snapshot duration and pane
count, but must not record process paths, command lines, session identifiers,
or repository information.

### Root process identity

Replace the UI layer's need to reinterpret a raw root process handle with a
narrow `RootProcessId` API on `ConptyConnection`. Keep the existing
`RootProcessHandle` API for its current consumers.

Only a connection that can provide a local root process ID is eligible for
automatic Copilot detection. Other `ITerminalConnection` implementations
report unsupported and never show a heuristic Copilot indicator.

### Pane-local state machine

`TerminalPaneContent` owns the observable Copilot state for one pane. The state
machine consumes four inputs:

- Whether a live `copilot.exe` exists in the pane's native process tree.
- The pane's current taskbar progress state.
- Bell or terminal-notification events.
- Whether the pane currently has focus.

State data:

```text
CopilotPaneActivity
  processPresent
  progressActive
  workObserved
  attentionLatched
  lastProgressRefresh
  processGeneration
```

`processGeneration` changes when the detected set of Copilot process identities
changes from empty to non-empty. It prevents a stale attention latch from one
Copilot invocation from being assigned to a later invocation.

Transitions:

| Input | Focused pane | Unfocused pane |
|---|---|---|
| Copilot appears | Observe only | Observe only |
| OSC 9;4 becomes indeterminate | Set working | Set working |
| OSC 9;4 refreshes | Keep working | Keep working |
| OSC 9;4 clears after work | Return to none | Latch attention |
| Copilot exits after work | Return to none | Latch attention |
| Copilot exits without observed work | Return to none | Return to none |
| Bell/notification while Copilot exists | Existing bell behavior only | Latch attention and preserve existing behavior |
| Owning pane gains focus | Clear attention | Not applicable |
| Connection restarts or pane closes | Clear all state | Clear all state |

Attention remains latched even after `copilot.exe` exits so the user can still
find the pane where work completed. It is tied to the process generation that
performed the work and clears when that pane is viewed or destroyed.

Working state is not latched. It requires:

- A live Copilot process.
- Current indeterminate OSC 9;4 progress.
- A progress refresh no older than 15 seconds.

The lease prevents stale working state after an abnormal process or transport
failure. Existing Copilot behavior refreshes progress every five seconds, so a
15-second lease tolerates two missed refreshes.

If taskbar progress changes directly from one nonzero state to another, only
state `3` is treated as Copilot working. Determinate, paused, and error taskbar
states remain available to their existing generic feature and do not create a
Copilot classification.

### Pane-to-tab propagation

Follow the existing taskbar and bell event path:

1. `ControlCore` exposes taskbar progress changes already produced by
   `TerminalCore`.
2. `TermControl` forwards the existing progress, bell, and notification events.
3. `TerminalPaneContent` combines those signals with process classification and
   raises a typed Copilot activity event when its pane state changes.
4. `Tab` subscribes for each leaf pane and aggregates all pane states.
5. `TerminalTabStatus` exposes the aggregate state and pane count.
6. `TabHeaderControl.xaml` renders one dedicated indicator.

Add a closed enum rather than several loosely related booleans:

```text
CopilotActivityState
  None
  Working
  Attention
```

Tab aggregation is:

```text
Attention if any pane is Attention
else Working if any pane is Working
else None
```

The aggregate count is the number of panes in the displayed state, not the
number of Copilot processes or sessions.

### Focus acknowledgement

Use pane focus, not tab selection, as the acknowledgement boundary.

When a pane gains focus:

1. Clear that pane's attention latch.
2. Recompute the tab aggregate.
3. Leave attention from sibling panes unchanged.

This matches the information Terminal actually owns. It also prevents a tab
with two panes from losing its indicator when the user has viewed only one of
them.

### Settings and rollout

Ship the first implementation behind an experimental global setting:

```json
"experimental.detectCopilotActivity": true
```

Reasons for an opt-in first release:

- Status is heuristic rather than producer-authenticated.
- Process scanning has performance and compatibility risk.
- WSL and remote panes are unsupported.
- The visual language needs user validation before becoming default behavior.

Do not add configurable output-text patterns. If executable variants beyond
`copilot.exe` are required, add a narrowly scoped executable-name allowlist in
a follow-up after validating the installation shapes. Do not inspect arbitrary
command lines by default.

## Implementation Sequence

### 1. Process classification foundation

Files and areas:

- `src/cascadia/TerminalConnection/ConptyConnection.idl`
- `src/cascadia/TerminalConnection/ConptyConnection.h`
- `src/cascadia/TerminalConnection/ConptyConnection.cpp`
- New TerminalApp process snapshot/classification helper and focused tests.

Deliverables:

- Expose a local root process ID.
- Build one native process snapshot into a guarded parent/child graph.
- Detect `copilot.exe` through direct and wrapper descendants.
- Register and unregister eligible pane roots without retaining raw process
  paths or command lines.

### 2. Pane activity state machine

Files and areas:

- `src/cascadia/TerminalApp/TerminalPaneContent.*`
- `src/cascadia/TerminalControl/ControlCore.*` only if a typed progress event
  payload is needed.
- `src/cascadia/TerminalControl/TermControl.*` only for event propagation not
  already exposed.

Deliverables:

- Add `CopilotActivityState`.
- Correlate process presence with existing taskbar state.
- Implement the 15-second working lease.
- Latch generic attention on unfocused work completion, exit, bell, or
  notification.
- Clear state on focus, connection replacement, restart, and pane destruction.

### 3. Tab aggregation and visual

Files and areas:

- `src/cascadia/TerminalApp/Tab.cpp`
- `src/cascadia/TerminalApp/Tab.h`
- `src/cascadia/TerminalApp/TerminalTabStatus.idl`
- `src/cascadia/TerminalApp/TerminalTabStatus.h`
- `src/cascadia/TerminalApp/TabHeaderControl.xaml`
- Localized resource files for tooltips and accessibility names.

Deliverables:

- Aggregate all leaf-pane states with attention above working.
- Track the count of panes in the displayed state.
- Add a dedicated tab header indicator without replacing the tab icon or
  generic progress ring.
- Add accessible names and conservative tooltip text.
- Clear only the focused pane's latch.

### 4. Experimental setting and lifecycle

Files and areas:

- Settings model and generated settings schema.
- TerminalPage or the narrowest existing application lifetime owner for the
  shared snapshot service.
- Pane creation, detach, restart, and connection replacement paths.

Deliverables:

- Add `experimental.detectCopilotActivity`, defaulting to `false`.
- Start process sampling only when enabled and eligible panes exist.
- Ensure detached panes transfer registration without duplicate ownership.
- Ensure closed or replaced connections cannot leave a status record.

### 5. Validation and measurement

Add focused tests for:

- Direct `copilot.exe` child detection.
- Shell-to-wrapper-to-`copilot.exe` descendant detection.
- Unrelated `copilot.exe` outside the pane tree being ignored.
- PID reuse and inaccessible process metadata.
- Process graph cycle/depth protection.
- Working activation and lease expiry.
- Focused completion not latching attention.
- Unfocused completion and process exit latching attention.
- A new Copilot invocation not inheriting stale working state.
- Bell and notification correlation.
- Two panes in one tab with different states.
- Acknowledging one pane while a sibling remains in attention.
- Pane close, detach, restart, and connection replacement cleanup.
- Setting disabled producing no process scans or indicators.

Manual scenarios:

1. Run released `copilot.exe` from PowerShell, switch tabs while it works, and
   confirm working becomes attention when progress clears.
2. Repeat through a native wrapper process.
3. Run Copilot in two panes and confirm aggregation and pane-specific
   acknowledgement.
4. Run a non-Copilot application that emits OSC 9;4 and confirm no Copilot
   indicator appears.
5. Run Copilot in WSL and confirm the unsupported case fails closed with no
   misleading indicator.

Performance acceptance criteria:

- One process snapshot per Terminal window sampling interval, not per pane.
- No UI-thread process enumeration.
- No scans while the setting is disabled or no eligible panes exist.
- No persistent storage of process inventories.

## Acceptance Criteria

The Terminal-only version is complete when:

- No Copilot CLI or Agent Runtime files are changed.
- A local released `copilot.exe` launched directly or through a native wrapper
  is associated with the correct pane.
- Existing Copilot OSC 9;4 progress shows pane and tab working state.
- Work that stops while its pane is unfocused creates a generic attention
  indicator.
- Focusing the owning pane clears only that pane's attention.
- Multiple pane states aggregate correctly into one tab indicator.
- Unsupported connection types and undetected process shapes show no indicator
  rather than a misleading status.
- The UI and documentation call the result activity or possible attention, not
  exact Copilot session status.

## Known Fidelity Limit

This plan does not fully satisfy the original goal of representing every
Copilot session inside a tab. Terminal can observe a pane's native process tree
and generic terminal progress, but it cannot observe Copilot's internal session
manager.

In particular, a background Copilot session can require permission or answer a
question without changing the foreground OSC 9;4 signal. That event is
undetectable to Terminal under this constraint.

The Terminal-only implementation is therefore a useful navigation aid for
foreground Copilot work, not a lossless session-status integration.

## Implementation Notes

- Product code is confined to Windows Terminal. No Copilot Agent Runtime
  changes are required.
- `CopilotProcessTracker` owns one application-wide native process snapshot
  cadence and invokes pane callbacks only when the classified process identity
  set changes.
- `CopilotPaneActivity` and `CopilotProcessSnapshot` are independent of XAML so
  their graph, lease, focus, and aggregation behavior can be covered by focused
  tests.
- `TerminalPaneContent` registers only enabled local ConPTY roots, consumes the
  existing OSC 9;4, bell, and notification event paths, and owns the attention
  latch.
- OSC 9;4 transitions capture their state, timestamp, and sequence before UI
  dispatch. A newly detected Copilot process generation accepts only a later
  progress transition.
- A shared pane event-ordering source captures the process classification
  boundaries immediately before and after Toolhelp snapshot acquisition.
  Attention within that acquisition interval remains pending until process
  lifetime evidence or a subsequent observation safely brackets it.
- Attention newer than the latest positive pre-snapshot boundary is never
  latched immediately. This keeps a later negative acquisition window able to
  classify the event as unresolved instead of making an irreversible decision
  from stale positive evidence.
- Bell and notification inputs capture their own sequence, timestamp, system
  time, and focus acknowledgement. Unresolved attention is retained until a
  shared process observation proves it belongs to the current Copilot
  generation or proves no Copilot process existed at the relevant boundary.
- Unresolved attention requests an immediate shared tracker snapshot.
  Requested observations report positive or negative classification even when
  the process set is unchanged. Attention already resolved by current process
  evidence does not request another scan.
- Shared observation requests use monotonic requested and completed
  generations. If a requested callback overlaps an active sample, the active
  sample clears its active flag and then unconditionally rechecks for
  unsatisfied generations. A request is therefore admitted by a new callback
  or explicitly rearmed before returning to the normal two-second cadence.
- Working lease scheduling rounds positive sub-millisecond remainders upward.
  Early or stale timer callbacks reschedule against the latest progress
  refresh when work remains active.
- The shared process tracker marks shutdown under its mutex, then cancels and
  drains its threadpool timer without holding that mutex. Registration,
  sampling, and timer scheduling cannot rearm the timer after shutdown begins.
- A terminal connection entering a closed or failed state resets Copilot
  activity with the ordering captured at the connection event. It does not
  model connection teardown as ordinary Copilot process completion.
- Disabling the experimental setting revokes registration, stops the working
  lease timer, resets activity with current ordering, and publishes `none`
  before any root-registration equality check.
- Root registration includes the creation time queried from the retained
  ConPTY process handle. Snapshot traversal rejects mismatched root identities
  and carries the latest known ancestor creation-time lower bound through
  wrappers with inaccessible creation times.
- `Tab` aggregates the exposed pane states and clears attention only through
  the focused pane's acknowledgement path.

## Quality Gates and Done Definition

- Focused process graph, pane reducer, tab aggregation, and settings tests pass.
- TerminalApp, TerminalConnection, and SettingsModel compile with generated
  C++/WinRT settings and IDL surfaces.
- The final diff contains no Copilot Runtime repository changes and preserves
  the unrelated vertical-tab changes already present in this worktree.
- Disabled settings and unsupported connection types create no process tracker
  registration and no tab indicator.

## Impact Scan

- `ConptyConnection` adds a read-only root process ID while preserving the
  existing root process handle API.
- `TerminalPaneContent` gains pane-local Copilot activity state and registers
  eligible local process roots with the shared tracker.
- `Tab` subscribes only to terminal pane activity events and aggregates all
  current leaf panes.
- `TerminalTabStatus` and `TabHeaderControl` add one dedicated visual,
  tooltip, and accessible name without changing the generic taskbar progress
  or bell indicators.
- The global settings model and schema add one experimental boolean that
  defaults to false.
- Detach preserves the pane content and its tracker registration; close,
  restart, connection termination, and disabling the setting revoke it.

## Convention Discovery

- Settings use `MTSM_GLOBAL_ONLY_SETTINGS` plus the matching
  `GlobalAppSettings.idl` inheritable property.
- Pane signals follow the existing `TermControl` to `TerminalPaneContent` to
  `Tab` event path.
- Tab status uses `WINRT_OBSERVABLE_PROPERTY` and XAML one-way bindings.
- Background work uses a native threadpool timer and marshals state changes to
  the pane's XAML dispatcher before touching pane or tab state.
- Tests use the existing TAEF projects for Settings Model and local
  TerminalApp behavior.

## Pre-Completion Interview

All implementation questions are resolved:

- Executable classification is limited to case-insensitive `copilot.exe`.
- Only local `ConptyConnection` roots are eligible.
- The sampling interval is two seconds and the working lease is 15 seconds.
- Attention remains generic and never claims permission, question, success, or
  failure precision.
- Pane focus is the acknowledgement boundary.
- The experimental setting remains hidden from Settings UI and defaults to
  false.

## Gut-Check Results

- Greenfield reframe: a shared snapshot service plus a pure reducer remains the
  preferred design because it prevents per-pane scans and keeps XAML out of the
  classification logic.
- Proportionality: the implementation has one platform-specific tracker, one
  pure activity module, and no plugin, factory, or configurable classifier.
- Sunk cost: existing taskbar and bell event paths are reused because they
  already carry the required pane-local signals, not merely because they
  exist.
