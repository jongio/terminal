# Test Plan: Terminal-Only Copilot Activity Indicators

## Status: COVERED
## Spec: doc/specs/Copilot session status/terminal-only-plan.md
## Created: 2026-09-02
## Updated: 2026-09-02

---

## Coverage Strategy

The process graph, pane activity reducer, and tab aggregation are isolated from
XAML and covered by local TerminalApp TAEF tests. The experimental setting is
covered by Settings Model deserialization and serialization tests. Generated
C++/WinRT, XAML bindings, connection integration, resources, and project wiring
are covered by targeted Debug x64 project builds.

The repository does not define a changed-line coverage threshold for these
C++ TAEF projects. Validation therefore uses direct branch and boundary tests
plus successful compilation of every changed product surface.

## Planned Tests

| ID | Behavior to verify | Source | Level | Test file -> name | Status |
|----|--------------------|--------|-------|-------------------|--------|
| T1 | Direct root `copilot.exe` is classified | Process classification | unit | `CopilotActivityTests.cpp` -> `FindsDirectCopilotProcess` | automated |
| T2 | Native wrapper descendants are traversed | Process classification | unit | `CopilotActivityTests.cpp` -> `FindsWrappedCopilotProcess` | automated |
| T3 | Unrelated Copilot processes are ignored | Fail-closed scope | unit | `CopilotActivityTests.cpp` -> `IgnoresUnrelatedCopilotProcess` | automated |
| T4 | Inaccessible creation time remains a valid unknown identity | PID reuse | unit | `CopilotActivityTests.cpp` -> `PreservesProcessIdentityWhenCreationTimeIsUnavailable` | automated |
| T5 | Process graph cycles and excessive depth terminate safely | Process classification | unit | `CopilotActivityTests.cpp` -> `GuardsCyclesAndDepth` | automated |
| T6 | OSC 9;4 indeterminate state starts working and expires after 15 seconds | Pane reducer | unit | `CopilotActivityTests.cpp` -> `TracksWorkingAndLeaseExpiry` | automated |
| T7 | Focused completion stays clear and unfocused completion latches attention | Pane reducer | unit | `CopilotActivityTests.cpp` -> `HandlesFocusedAndUnfocusedCompletion` | automated |
| T8 | A later invocation does not inherit stale working state | Process generation | unit | `CopilotActivityTests.cpp` -> `DoesNotCarryWorkingIntoNewInvocation` | automated |
| T9 | A PID reused with a new creation time does not inherit working state | PID reuse | unit | `CopilotActivityTests.cpp` -> `DoesNotCarryWorkingAcrossPidReuse` | automated |
| T10 | Bell or notification attention requires a Copilot process and clears on focus | Pane reducer | unit | `CopilotActivityTests.cpp` -> `CorrelatesAttentionSignals` | automated |
| T11 | Attention outranks working and the count reflects the displayed state | Tab aggregation | unit | `CopilotActivityTests.cpp` -> `AggregatesPaneActivity` | automated |
| T12 | Focusing one pane leaves sibling attention intact | Focus acknowledgement | unit | `CopilotActivityTests.cpp` -> `ClearsOnlyFocusedPaneAttention` | automated |
| T13 | Restart, close, or replacement reset clears activity | Lifecycle | unit | `CopilotActivityTests.cpp` -> `ResetClearsActivity` | automated |
| T14 | The experimental setting defaults false and accepts true | Settings | unit | `DeserializationTests.cpp` -> `TestCopilotActivitySetting` | automated |
| T15 | The experimental setting round-trips through generated serialization | Settings | unit | `SerializationTests.cpp` -> `GlobalSettings` | automated |
| T16 | IDL, XAML, resources, tracker, pane, tab, and settings surfaces compile together | Integration | build | `TerminalConnection.vcxproj`, `TerminalAppLib.vcxproj`, focused test projects | automated |
| T17 | Fast captured `3 -> 0` progress transitions are applied without rereading mutable control state | Progress ordering | unit | `CopilotActivityTests.cpp` -> `AppliesCapturedProgressTransitions` | automated |
| T18 | A new process generation rejects progress observed before its generation barrier | Progress ordering | unit | `CopilotActivityTests.cpp` -> `RequiresFreshProgressForNewInvocation` | automated |
| T19 | A reused root PID with a different creation time is rejected | PID reuse | unit | `CopilotActivityTests.cpp` -> `RejectsReusedRootProcess` | automated |
| T20 | A child process older than its purported parent is excluded | PID reuse | unit | `CopilotActivityTests.cpp` -> `RejectsChildOlderThanParent` | automated |
| T21 | Progress newer than a classification-time barrier is accepted even when applied before the process callback | Progress ordering | unit | `CopilotActivityTests.cpp` -> `AcceptsProgressAfterCapturedClassificationBarrier` | automated |
| T22 | Progress completion uses event-time focus and later focus acknowledgement | Focus ordering | unit | `CopilotActivityTests.cpp` -> `UsesFocusAtProgressEventTime` | automated |
| T23 | Process completion remains acknowledged when focus occurs before callback dispatch | Focus ordering | unit | `CopilotActivityTests.cpp` -> `AcknowledgesQueuedProcessCompletion` | automated |
| T24 | Reset clears latched attention using current sequence and focus acknowledgement | Disable cleanup | unit | `CopilotActivityTests.cpp` -> `ResetClearsActivity` | automated |
| T25 | Positive sub-millisecond lease time rounds upward and an early expiry remains working | Lease timing | unit | `CopilotActivityTests.cpp` -> `RoundsSubMillisecondLeaseUp` | automated |
| T26 | A stale timer callback after progress refresh preserves work and the refreshed deadline | Lease timing | unit | `CopilotActivityTests.cpp` -> `StaleLeaseCallbackPreservesRefreshedWork` | automated |
| T27 | Terminal connection teardown resets working state without latching attention | Lifecycle | unit | `CopilotActivityTests.cpp` -> `ConnectionTeardownClearsWorking` | automated |
| T28 | Timer cancellation blocks until a running callback drains | Tracker lifetime | unit | `CopilotActivityTests.cpp` -> `DrainsRunningProcessTimerCallback` | automated |
| T29 | Attention before first positive classification replays only when process creation proves Copilot existed | Attention ordering | unit | `CopilotActivityTests.cpp` -> `ReplaysAttentionBeforePositiveClassification` | automated |
| T30 | Attention after snapshot acquisition replays when the positive callback is pending | Attention ordering | unit | `CopilotActivityTests.cpp` -> `ReplaysAttentionAfterSnapshotBoundary` | automated |
| T31 | Negative evidence, generation changes, and reset reject stale pending attention | Attention ordering | unit | `CopilotActivityTests.cpp` -> `RejectsStalePendingAttention` | automated |
| T32 | Attention inside a positive snapshot acquisition window remains pending until a later safe observation | Attention ordering | unit | `CopilotActivityTests.cpp` -> `KeepsAttentionPendingDuringSnapshotAcquisition` | automated |
| T33 | Acquisition-window and post-absence attention are not replayed onto a later process generation | Attention ordering | unit | `CopilotActivityTests.cpp` -> `RejectsStaleAttentionFromSnapshotAcquisition` | automated |
| T34 | An immediate request overlapping a running sample requires an immediate follow-up generation | Tracker scheduling | unit | `CopilotActivityTests.cpp` -> `RequestsRerunForOverlappingObservation` | automated |
| T35 | Attention inside a later negative acquisition window remains pending despite prior positive evidence | Attention ordering | unit | `CopilotActivityTests.cpp` -> `DefersAttentionAcrossPositiveToNegativeAcquisition` | automated |
| T36 | A request arriving during the sampling completion handoff remains observable by the unconditional post-clear recheck | Tracker scheduling | unit | `CopilotActivityTests.cpp` -> `RequestsRerunDuringSamplingHandoff` | automated |

## Functionality Inventory

| # | Functionality introduced | Location | Covered by | Status |
|---|--------------------------|----------|------------|--------|
| F1 | Root process ID projection backed by an atomic process ID | `ConptyConnection.idl`, `ConptyConnection.cpp` | T16 | covered |
| F2 | Process identity, snapshot graph, and descendant classifier | `CopilotActivity.h`, `CopilotActivity.cpp` | T1-T5, T9 | covered |
| F3 | Shared two-second process snapshot cadence with overlap prevention | `CopilotProcessTracker.h`, `CopilotProcessTracker.cpp` | T16 | covered |
| F4 | Start on first registration and stop on last unregister | `CopilotProcessTracker.cpp` | T16 | covered |
| F5 | Pane working lease and completion attention transitions | `CopilotActivity.cpp` | T6-T9 | covered |
| F6 | Bell and notification correlation | `CopilotActivity.cpp`, `TerminalPaneContent.cpp` | T10, T16 | covered |
| F7 | UI-thread serialization of pane reducer updates | `TerminalPaneContent.cpp` | T16 | covered |
| F8 | Disabled and unsupported connections fail closed | `TerminalPaneContent.cpp` | T14, T16 | covered |
| F9 | Pane focus acknowledgement and lifecycle cleanup | `TerminalPaneContent.cpp`, `Tab.cpp` | T7, T12-T13, T16 | covered |
| F10 | Tab aggregation with attention priority and displayed-state count | `Tab.cpp` | T11-T12, T16 | covered |
| F11 | Observable tab status and dedicated accessible indicator | `TerminalTabStatus.idl`, `TabHeaderControl.xaml` | T16 | covered |
| F12 | Conservative localized tooltip and accessible text | `Resources/en-US/Resources.resw`, `Tab.cpp` | T16 | covered |
| F13 | Experimental setting model and schema | `MTSMSettings.h`, `GlobalAppSettings.idl`, `profiles.schema.json` | T14-T16 | covered |
| F14 | Event-time progress capture with monotonic ordering and generation freshness | `CopilotActivity.h`, `TerminalPaneContent.cpp` | T17-T18 | covered |
| F15 | Root identity and parent-child creation-time validation | `ConptyConnection.cpp`, `CopilotActivity.cpp`, `CopilotProcessTracker.cpp` | T19-T20 | covered |
| F16 | Classification-time ordering capture and post-barrier progress replay | `CopilotActivity.cpp`, `CopilotProcessTracker.cpp` | T21 | covered |
| F17 | Event-time focus and acknowledgement-aware completion | `CopilotActivity.cpp`, `TerminalPaneContent.cpp` | T22-T23 | covered |
| F18 | Explicit enable and disable lifecycle cleanup | `TerminalPaneContent.cpp` | T24 | covered |
| F19 | Rounded lease scheduling and stale timer callback recovery | `CopilotActivity.cpp`, `TerminalPaneContent.cpp` | T25-T26 | covered |
| F20 | Connection terminal-state teardown reset | `TerminalPaneContent.cpp` | T27 | covered |
| F21 | Ordering capture at the Toolhelp snapshot acquisition boundary | `CopilotProcessTracker.cpp` | T21 | covered |
| F22 | Tracker shutdown prevents rearming and drains running timer callbacks | `CopilotProcessTracker.cpp` | T28 | covered |
| F23 | Ordered attention capture, pending replay, and negative observation invalidation | `CopilotActivity.cpp`, `TerminalPaneContent.cpp` | T29-T31 | covered |
| F24 | Shared requested observations emit unchanged positive or negative classifications | `CopilotProcessTracker.cpp` | T29-T31 | covered |
| F25 | Pre-snapshot and post-snapshot ordering boundaries preserve acquisition-window ambiguity | `CopilotActivity.cpp`, `CopilotProcessTracker.cpp` | T32-T33 | covered |
| F26 | Requested and completed observation generations preserve overlapping immediate requests | `CopilotProcessTracker.h`, `CopilotProcessTracker.cpp` | T34 | covered |
| F27 | Positive evidence remains provisional until a later observation safely brackets attention | `CopilotActivity.cpp` | T35 | covered |
| F28 | Sampling completion always rechecks requested generations after clearing the active flag | `CopilotProcessTracker.cpp` | T36 | covered |

## Gaps and Additions

- The reconciliation pass added T9 after identifying the non-empty PID-reuse
  replacement path.
- The review-fix pass added T17-T20 for captured progress ordering, fresh
  generation progress, root PID reuse, and stale parent-child edges.
- The final-review pass added T21-T24 for classification dispatch races,
  event-time focus semantics, acknowledgement before process callback
  dispatch, and disable cleanup.
- The final blocker pass added T25-T27 for sub-millisecond lease rounding,
  stale timer callbacks, and connection teardown.
- The acceptance-blocker pass added T28 for threadpool timer cancellation and
  callback drain behavior.
- The ordered-attention pass added T29-T31 for pre-classification replay,
  in-flight snapshot replay, and stale-event rejection.
- The final attention pass added T32-T34 for acquisition-window ambiguity,
  post-absence stale rejection, and overlapping immediate observation reruns.
- The final handoff pass added T35-T36 for positive-to-negative acquisition
  windows and requests arriving during sample completion.
- No remaining functionality gaps were found.

## Evidence

```text
GATE EVIDENCE:
  artifact: test-plan
  path: doc/specs/Copilot session status/test-plan.md
  status: COVERED
  planned_tests: 36 (36 automated)
  functionality_units: 28
  gaps_found: 21
  gaps_closed: 21
  coverage_new_lines: not measured; repository has no configured changed-line threshold for these projects
```
