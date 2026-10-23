# Copilot Session Status in Terminal Tabs

> This document describes the higher-fidelity design that requires Copilot CLI
> producer changes. The Terminal-only implementation plan, which requires no
> Copilot changes, is documented in
> [terminal-only-plan.md](terminal-only-plan.md).

## Overview

Windows Terminal tabs can contain multiple panes, and each pane can host a
Copilot CLI process with multiple foreground and background sessions. Users
need a tab-level indicator that shows where Copilot is working and where a
session needs attention without opening every tab.

The integration will be event-driven. Copilot CLI will publish a small,
versioned status snapshot for each live Copilot session through the pane's
terminal output. Windows Terminal will receive the snapshot through its
existing VT parser, bind it to the receiving pane, and aggregate all session
states across all panes in the tab.

The design does not introduce a daemon, filesystem registry, process scan, or
polling loop. It also does not overload taskbar progress or bell semantics.

### Goals

- Represent multiple Copilot sessions in one pane and multiple panes in one
  tab.
- Distinguish active work, unresolved user interaction, unseen completion, and
  unseen failure.
- Keep status ownership local to the pane that received the signal.
- Clear stale status after process failure, pane closure, or protocol silence.
- Preserve existing Copilot terminal suspend/resume behavior.
- Ignore unsupported or malformed messages without affecting terminal output.

### Non-goals

- Display session names, prompts, repository names, or other user content in
  terminal chrome.
- Let Terminal control or answer a Copilot session.
- Discover Copilot sessions outside the terminal's own panes.
- Synchronize session status between different Terminal windows or devices.
- Provide a general cross-application job protocol in the first version.

## Existing Infrastructure

### Copilot Agent Runtime

Copilot already derives the required state from canonical session events:

- `BackgroundSessionStatus` defines `thinking`, `waiting`, `done`, and `error`
  in `src/cli/sessions/backgroundSessionManager.ts`.
- `SessionAttentionKind` defines `permission` and `question`.
- `sessionAttention.ts` derives pending question state from ask-user,
  elicitation, and plan-approval request stores.
- Permission and question state is maintained from paired requested/completed
  events.
- `useTerminalProgress.ts` demonstrates lifecycle-safe, periodically refreshed
  out-of-band terminal state.
- `src/cli/terminal/terminal.ts` is the process-wide owner of OSC emission,
  terminal capability checks, suspension, reset, and restoration.

The design will reuse these sources rather than infer status from rendered
text, spinner frames, process CPU usage, or shell state.

### Windows Terminal

Windows Terminal already has the required pane-local propagation pattern:

1. `OutputStateMachineEngine` routes an OSC command.
2. `AdaptDispatch` validates the command and calls `ITerminalApi`.
3. `TerminalCore` stores pane-local state and invokes a callback.
4. `ControlCore` raises a typed C++/WinRT event.
5. `TermControl` and `TerminalPaneContent` forward the event.
6. `Tab` gathers states from every leaf pane and computes one tab state.
7. `TerminalTabStatus` exposes observable properties to tab header XAML.

Taskbar progress provides the propagation and aggregation precedent. Bell
status provides the focus-acknowledgement precedent. Copilot status needs its
own type because its state, lifetime, and priority rules differ from both.

## Architecture

### Components

#### Copilot session status coordinator

The Copilot CLI owns one process-wide coordinator keyed by session lifetime.
It converts canonical session events into one normalized wire state per live
session:

| Copilot state | Wire state | Lifetime |
|---|---|---|
| Thinking or processing | `working` | Leased |
| Pending permission | `permission` | Leased |
| Pending ask-user, elicitation, or plan approval | `question` | Leased |
| Successfully finished work | `done` | Latched by Terminal |
| Terminal session failure | `error` | Latched by Terminal |
| Waiting with no unseen outcome or attention | Withdrawn | Hidden |
| Session closed or disposed | Closed | Removed |

Permission takes precedence over question inside a session. Outstanding
request identifiers must be tracked so completing one request does not clear
another pending request.

Only root-session state is published for a session. Subagent activity affects
the owning session's working state through the existing session lifecycle; it
does not create independent Terminal records.

The coordinator, rather than foreground or background UI components, owns:

- The stable wire token for the session lifetime.
- Status reduction inputs from existing session lifecycle owners.
- The current normalized state.
- A monotonically increasing status generation.
- Publisher registration and disposal.

Moving a session between foreground and background presentation does not
replace its token, reset its generation, clear its status, or recreate its
status inputs. Foreground and background managers report lifecycle ownership
changes to the coordinator but do not own wire state.

The coordinator must not independently subscribe to `user_input.*` or
`elicitation.*`. Listener presence advertises runtime capabilities, so an
extra listener under `--no-ask-user` can create a request with no responder.
Question state is supplied by the existing responder-gated subscriptions and
pending-request stores, carrying the session's
`promptEventsEnabled`/`askUser` decision. Permission and plan-approval signals
are similarly reused through their existing lifecycle owners.

`error` is not produced from every generic `session.error` event. Copilot adds
a closed terminal-outcome classifier at the existing turn-completion/error
handling boundary. It emits failure only when the current work generation has
conclusively terminated unsuccessfully. Recoverable model attempts, IDE
connection notices, failures affecting another session, and operational
warnings that permit the turn to continue do not produce terminal `error`.
A later work generation supersedes a prior failure with `working`.

#### Copilot terminal status publisher

The publisher is part of the existing lifecycle `Terminal` abstraction. It
must not write directly through React output or `process.stdout`.

Responsibilities:

- Assign an opaque, random wire token to each session lifetime. The actual
  Copilot session identifier is not transmitted.
- Emit a complete, idempotent snapshot when a session's normalized state
  changes.
- Increment the status generation only when normalized state changes. Periodic
  refresh and suspend/resume replay retain the same generation.
- Refresh leased states every five seconds.
- Close every registered session during final process shutdown and session
  disposal.
- Record desired state while the terminal is suspended and replay it when the
  CLI regains the TTY.
- Emit only when stdout is a TTY, OSC is accepted, and Windows Terminal is
  positively detected.

The first version emits only when the cached terminal type is
`windows-terminal`, `WT_SESSION` is present, and `TMUX`, `STY`,
`SSH_CONNECTION`, and `SSH_TTY` are absent. Multiplexer passthrough can be
added later through explicit capability detection without changing the status
model.

#### Windows Terminal OSC handler

The protocol extends the existing Windows Terminal action namespace, OSC
`9001`, with a `CopilotSessionStatus` action.

The logical `set` message fields are:

1. Action: `CopilotSessionStatus`
2. Protocol version: `1`
3. Operation: `set`
4. Opaque session token
5. Unsigned status generation
6. State for `set`: `working`, `permission`, `question`, `done`, or `error`

The logical `withdraw` message contains the action, version, operation,
session token, and generation. It removes visible state without ending the
session lifetime. Waiting with no outcome uses `withdraw`.

The logical `close` message contains the action, version, operation, session
token, and final generation. It permanently ends that token's lifetime.
Tokens are never reused by the producer.

Messages use the standard OSC string terminator. Version 1 carries no
free-form text, percentages, file paths, URLs, or session metadata.

The handler validates the entire message before changing state:

- Exact supported version and field count.
- Closed `set`, `withdraw`, and `close` operation enum and closed state enum.
- ASCII base64url session token with a maximum length of 64 characters.
- Unsigned 32-bit generation.
- Maximum complete payload length of 256 code units.
- Maximum of 64 live Copilot session records per pane.

The 256-code-unit limit is enforced while the VT state machine accumulates OSC
9001 input, before allocation can grow without bound. Once the limit is
exceeded, the parser discards input until the OSC terminator and does not call
`AdaptDispatch`. The adapter repeats the bound and schema validation as a
defense-in-depth check.

Malformed, unsupported, oversized, or excess records are ignored atomically.
The receiving pane/control is the security and ownership boundary. A payload
cannot select another pane, tab, window, or connection.

#### Automatic pane and session mapping

No shell registration or process discovery is required.

- The ConPTY/TerminalCore instance that receives the OSC identifies the pane.
- The opaque token identifies one Copilot session within that pane.
- The existing pane tree identifies the tab that owns the pane.

The first valid `set` creates the `(pane, token)` association. `withdraw`
hides its current status, and `close` ends the association.

Starting `copilot` directly works because its stdout is the pane's PTY. A
wrapper also works when it leaves Copilot attached to that PTY and passes
control sequences through unchanged. A wrapper that captures, strips,
redirects, buffers indefinitely, or detaches Copilot stdout must forward the
status sequences; Terminal cannot infer an association outside the pane's
output stream.

#### Pane-local status registry

Each `TerminalCore` instance stores the latest status by opaque session token.
Records contain:

- State.
- Latest producer generation.
- Last refresh time for leased states.
- Last acknowledged outcome generation for the token.

`working`, `permission`, and `question` expire after 15 seconds without a
refresh. This allows two missed five-second refreshes before cleanup.

`done` and `error` do not expire on the active-state timer. They remain latched
until their pane is viewed, the session sends `close`, or a 24-hour defensive
retention limit is reached. Closing or restarting the terminal connection
removes every record owned by that pane.

Refreshes with the same generation update only the lease deadline. They do not
create a new outcome or reverse acknowledgement. A generation lower than the
latest accepted or acknowledged generation is ignored. A higher generation is
a new state transition; a higher `done` or `error` generation is initially
unacknowledged.

Acknowledging an outcome removes its active visual state and retains the
per-token acknowledgement generation in the live session record. A replay at
or below that generation is ignored. `withdraw` preserves the record and its
acknowledgement generation. `close` removes it because PTY output is ordered:
all earlier messages for that token have already been processed, and the
producer never reuses the token.

The 64-record limit applies to complete live session records, including
acknowledged sessions. Existing-token updates are always accepted. Before
admitting a new token, Terminal removes expired records and records whose
ordered `close` has been received. If the bound remains full, the new token is
rejected. This intentionally favors a strict memory bound over availability
against a process already able to emit arbitrary pane-local OSC controls.

Each control owns one deadline scheduler for the earliest leased or retention
deadline. Status mutation, deadline selection, and expiry run under the
TerminalCore lock. The scheduler uses a monotonic clock, is rearmed whenever
the earliest deadline changes, and is cancelled on control teardown or
connection replacement. Its callback purges expired records, raises the
existing status-change callback when the visible summary changes, and uses the
normal control-to-UI dispatcher path for tab aggregation.

Terminal does not trust a sender-provided timestamp, process identifier,
pane identifier, or expiry.

#### Tab status aggregator

`Pane` recursively collects a summary from every leaf pane. `Tab` computes one
visible state with this priority:

1. `permission`
2. `question`
3. Unacknowledged `error`
4. Unacknowledged `done`
5. `working`
6. None

The ordering prioritizes interactions that currently block progress, followed
by unseen terminal outcomes, followed by informational activity.

When several records have the same priority, the record with the highest local
acceptance order wins. The visual only represents the aggregate state; it does
not expose the winning token.

#### Focus acknowledgement

An outcome is considered viewed when its owning pane is focused while its tab
is selected. In a single-pane tab, selecting the tab satisfies both
conditions. Selecting a tab does not acknowledge outcomes in its unfocused
split panes.

Focusing the owning pane acknowledges its currently latched `done` and `error`
records by recording their generation watermarks and removing their active
records.

If `done` or `error` arrives for the already-focused pane of the selected tab,
the UI event carries the exact `{pane, token, generation}` snapshot that was
presented. Terminal queues acknowledgement on the tab's dispatcher and applies
it only if that pane remains focused and the registry still contains the same
token and generation. Outcomes arriving in unfocused panes remain latched.

`permission` and `question` are never cleared or acknowledged by focus. They
remain visible until Copilot reports that the request was resolved or their
lease expires.

`working` always mirrors current leased state and is not acknowledged.

If a session publishes a higher-generation `done` or `error` update after
acknowledgement, it is a new outcome and the indicator reappears unless the
owning pane is already focused.

#### Tab presentation

`TerminalTabStatus` gains a dedicated Copilot status property rather than
reusing `BellIndicator` or `IsProgressRingActive`.

The tab header displays one compact icon in a dedicated status slot:

| State | Meaning | Suggested visual |
|---|---|---|
| `working` | Copilot is processing | Copilot glyph with activity animation |
| `permission` | Approval is required | Warning or exclamation badge |
| `question` | User input is required | Question badge |
| `done` | Work completed since last view | Success badge |
| `error` | Work failed since last view | Error badge |

The icon has localized automation text and tooltip text. Color is not the only
means of distinguishing states. Motion respects the existing system animation
settings.

The dedicated slot is outside the tab icon presenter that
`_UpdateProgressState` hides when the progress ring is active. Task progress
and Copilot attention can therefore coexist. Header layout may collapse
lower-priority informational indicators when space is constrained, but it must
not hide `permission`, `question`, or `error`.

## Data Flow

1. A Copilot session event changes the reducer's normalized state.
2. The lifecycle terminal publishes a complete snapshot for that session.
3. ConPTY delivers the OSC message as output from one pane.
4. Windows Terminal validates the message in `AdaptDispatch`.
5. `TerminalCore` updates the receiving pane's registry and raises a change
   callback.
6. The control and pane-content layers forward a typed change event.
7. `Tab` recomputes the aggregate across all leaf panes.
8. `TerminalTabStatus` notifies XAML bindings.
9. Focusing the owning pane acknowledges latched outcomes and triggers another
   aggregation.

Temporary `Terminal.stop()`/`start()` suspension does not send `close` and
does not destroy Terminal-side records. The publisher records desired state
while another process owns the TTY and replays the same generation after
resume. Final shutdown or session disposal sends ordered `close` before
releasing the TTY. A new work transition increments generation; a waiting
transition sends `withdraw`.

Connection replacement clears the registry and acknowledgement state
synchronously in `ControlCore::Connection(newConnection)` before the new
output handler is attached. Terminal connection closure performs the same
clear. Both paths raise a status-change event so tab aggregation cannot retain
state from the previous process.

Pane-tree mutations, content detach/attach, pane closure, pane moves, and
connection restart explicitly trigger reaggregation even when no status
message is received.

## Patterns and Decisions

### Event-driven publishing over polling

Copilot already has ordered session lifecycle events. Publishing on transitions
is cheaper and more accurate than Terminal probing processes, sockets, files,
or shell commands.

### Per-session snapshots over process aggregates

One Copilot process can host multiple sessions with different states.
Per-session snapshots preserve that distinction and allow correct cleanup and
priority aggregation. A process-wide coordinator preserves the same session
identity across foreground/background UI handoffs.

### Pane-bound ownership

The terminal associates status with the control that received the bytes.
Session tokens distinguish records only within that pane. They are not global
identities or authorization credentials.

### Idempotent snapshots over transition commands

Repeated `set` messages with the same generation converge on the latest state
and make periodic refresh safe. Producer generations distinguish replay from a
new outcome without requiring a transition history. `withdraw` changes
visibility without destroying replay protection; ordered `close` ends the
token lifetime.

### Separate Copilot status from task progress and bells

OSC `9;4` controls generic progress and the Windows taskbar. BEL is governed by
user notification settings and is edge-triggered. Neither can represent
multiple durable Copilot session states without semantic collisions.

### Leased active state and latched outcomes

Leases prevent stale working or blocking indicators after abnormal process
termination. Terminal-side latching and per-session acknowledgement
generations make unseen completion and failure useful without allowing
suspend/resume replay to resurrect an outcome.

## Gap Remediation

### Copilot gaps

- Add a process-wide normalized session-status coordinator over existing
  canonical events.
- Add lifecycle-terminal APIs for publishing, refreshing, restoring,
  withdrawing, and closing per-session state.
- Add conservative Windows Terminal capability gating.
- Add focused tests for event precedence, multiple pending requests,
  suspend/resume, reset, and exact OSC output.

No new Rust session contract is required for the initial CLI integration.

### Windows Terminal gaps

- Extend `DoWTAction` with the versioned status action and strict parser.
- Enforce the OSC 9001 size bound during parser accumulation.
- Add a typed Copilot status model and registry to TerminalCore.
- Add one monotonic deadline scheduler per control and explicit cleanup during
  connection replacement and closure.
- Propagate a change event through ControlCore, TermControl, and
  TerminalPaneContent.
- Add pane collection and tab aggregation using the existing taskbar-state
  pattern.
- Add focused-pane acknowledgement using the existing bell pattern and
  producer-generation watermarks.
- Add an observable tab-status property, localized accessibility strings, and
  tab header visual.

## Testing Strategy

### Copilot

- Reducer tests for every canonical state transition and priority.
- Multiple-request tests proving one completion does not clear another.
- Lifecycle-terminal tests for exact bytes, generations, idempotent refresh,
  withdraw, ordered close, suspension, restore, final shutdown, unsupported
  terminals, and timer refresh.
- Hook/application integration tests proving foreground and background
  sessions are all published without changing identity during handoff.

### Windows Terminal

- Parser tests proving oversized and unterminated OSC 9001 input is discarded
  before unbounded accumulation.
- Adapter tests for every valid operation/state and every malformed input
  class.
- TerminalCore tests for generation ordering, replay after acknowledgement,
  set, replacement, clear, admission/eviction, lease expiry, retention expiry,
  scheduler cancellation, and connection reset.
- Pane/tab tests for multiple sessions, multiple panes, priority ordering,
  focused-pane acknowledgement, outcomes arriving while focused, structural
  pane changes, and new outcome generations.
- XAML/local tests for icon state, tooltip, automation text, constrained
  layout, high contrast, and reduced motion.

## Rollout and Compatibility

The Terminal consumer can ship before the Copilot producer because unknown OSC
actions are ignored. The Copilot producer emits only when Windows Terminal is
positively detected and provides a configuration/environment escape hatch to
disable the integration.

The protocol version permits additive future evolution. Version 1 parsers must
ignore unsupported versions rather than interpreting them partially.

No persisted user setting is required in Windows Terminal for the first
version. Existing users who do not run a compatible Copilot CLI observe no
behavior change.

## Risks and Trade-offs

### OSC spoofing

Any process that can write to a pane can emit terminal control sequences. The
design limits impact to a cosmetic status owned by that pane, accepts no
free-form text, and enforces strict size and count bounds.

### Abnormal termination

A killed process may not send clear messages. Leases clear active and blocking
states, while pane connection teardown clears all records. Latched outcomes
are only created by explicit complete messages and have a defensive retention
limit.

### Multiplexers and remote sessions

Private OSC forwarding varies across tmux, screen, SSH, and nested terminals.
The initial design fails closed in those environments. This sacrifices coverage
to avoid writing unsupported control sequences or assigning status to the
wrong outer pane.

### Header density

Tabs already contain several status indicators. The Copilot indicator uses one
aggregate icon and deterministic priority rather than one icon per session.

### Version skew

Older Terminal versions ignore the new OSC action. Newer Terminal versions
ignore unsupported protocol versions. Copilot capability gating prevents
unnecessary output where support is unknown.

## Decisions Log

| Decision | Resolution | Rationale |
|---|---|---|
| Transport direction | Copilot pushes status through terminal output | Reuses canonical events and avoids polling or a daemon |
| Protocol namespace | Versioned `CopilotSessionStatus` action under OSC 9001 | Reuses Windows Terminal's private action namespace without colliding with progress |
| Granularity | One opaque record per Copilot session | Supports multiple sessions per process and panes per tab |
| Identity | Random session-lifetime wire token | Avoids exposing persistent Copilot session identifiers |
| Attention acknowledgement | Focusing the owning pane acknowledges only latched `done` and `error` | Highlights unseen outcomes without hiding unresolved interaction or outcomes in other split panes |
| Replay identity | Producer generation increments only on normalized state transitions | Suspend/resume and periodic refresh cannot resurrect acknowledged outcomes |
| Session presentation handoff | Process-wide coordinator retains token, subscriptions, state, and generation | Foreground/background UI changes cannot duplicate or lose status |
| Active-state cleanup | Five-second refresh with 15-second terminal lease | Handles abnormal exits while tolerating missed refreshes |
| Temporary TTY release | Preserve records and replay the same generation | Shell-out and suspend/resume do not erase unseen outcomes |
| Session end | Ordered `close` with a never-reused token | Safely removes status and acknowledgement state |
| Outcome retention | Per-session acknowledgement generation plus 24-hour defensive retention | Bounds stale records while preserving unseen outcomes |
| Connection replacement | Clear before attaching the new output handler | Prevents state from the previous process surviving pane restart |
| Direct and wrapped launch | Association follows the pane's PTY output path | No shell registration, PID scan, or wrapper-specific API is required |
| Initial multiplexer support | Fail closed | Avoids incorrect pane ownership and unsupported escape output |
| Terminal visual | One aggregate Copilot indicator per tab | Keeps header density bounded |
