# Architecture: Configurable Tab Strip Positions

## Overview

Windows Terminal captures a window's tab-strip position at construction and composes existing settings, titlebar, tab-management, and XAML infrastructure around that value. The design supports Top, Bottom, Left, and Right while preserving the current window and terminal sessions when settings reload.

The implementation deliberately treats orientation as a window-lifecycle decision. Runtime horizontal/vertical TabView template switching was rejected because WinUI did not reliably re-realize existing tab items, while recreating the host window would invalidate native and terminal-control relationships.

## Architecture

### Settings contract

`TerminalSettingsModel` owns the `TabPosition` enum, root JSON mapping, default, inheritance, and schema contract. `TerminalSettingsEditor` binds the four choices through the existing enum-entry view-model pattern and uses localized relaunch-required copy.

### Window construction

`AppHost` and `TerminalWindow` read the initial position before native titlebar construction and launch sizing. Top may use non-client titlebar hosting when enabled. Bottom, Left, and Right remain in the XAML client area.

### Page composition

`TerminalPage` caches the initial position and rearranges existing `TabRowControl`, info-bar, terminal-content, and overlay elements:

- Top: existing horizontal titlebar/client row.
- Bottom: horizontal client row below content.
- Left/Right: vertical sidebar, accessible resize handle, and terminal-content column.

Deferred overlays remain children of the page root and span the full layout so dialogs, command palette, suggestions, teaching tips, and info bars remain independent of tab position.

### Vertical presentation

`VerticalTabViewStyle.xaml` supplies dedicated TabView and TabViewItem templates. It reuses existing theme resources and tab data rather than introducing a second tab model. `TabManagement` continues to own tab creation, selection, hiding, and drag behavior.

### Orientation-aware language

`Tab` maps logical Forward and Backward operations to visible-axis terms. Horizontal strips use Left/Right; vertical strips use Up/Down. Generic action configuration remains orientation-neutral.

## Patterns & Decisions

1. **Root setting, not theme setting.** Built-in themes are omitted during Settings UI serialization, so the root window setting is the reliable persistence boundary.
2. **Launch-time application.** The current window never changes orientation after settings reload; the UI states the relaunch requirement.
3. **Reuse one tab collection.** Layout changes presentation and geometry only, avoiding duplicated tab state.
4. **Self-contained vertical templates.** Cross-resource style references that can fail at XAML load time are avoided.
5. **Accessible resize handle.** The splitter is a focusable control with localized automation name, help text, current-width status, arrow-key resizing, bounded width, and pointer cancellation handling.

## Gap Remediation

- All four enum values and the omitted-value default are covered in Settings Model tests.
- Existing local tests cover session preservation and orientation-aware tab text.
- The resize handle now rejects non-primary pointer presses, resets state on capture loss/cancellation, and supports keyboard resizing.
- Packaged-app verification remains the correct layer for visual opacity, DPI, native titlebar, and full interaction-matrix checks.

## Risks & Trade-offs

- Custom TabView templates must be reviewed when the repository updates its WinUI version.
- Top titlebar hosting crosses native and XAML boundaries and must remain a construction-time decision.
- Vertical width is intentionally process-local and not serialized; this limits settings complexity but does not preserve a user's resized width.
- Manual packaged-app coverage is required for visual and native-window behavior that TAEF does not expose reliably.

## Decisions Log

| Decision | Resolution | Rationale |
|----------|------------|-----------|
| Apply position live or after relaunch | After relaunch | Protects realized tabs, active sessions, XAML island, and native window identity |
| Store under themes or root settings | Root settings | Persists through Settings UI serialization |
| Recreate tab state for vertical mode | No | Presentation-only composition is safer and simpler |
| Persist sidebar width | No | Not required for the initial feature |
| Splitter accessibility | Keyboard-focusable resize handle | Provides pointer, keyboard, and automation discoverability |

No blocking design questions remain.
