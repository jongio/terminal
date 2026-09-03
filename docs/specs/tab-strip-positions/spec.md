---
author: Jon Gallant
status: approved
---

# Configurable Tab Strip Positions

## Problem

Windows Terminal fixes its tab strip to the top edge, which is inefficient for users who work on wide displays, keep many tabs open, or align Terminal beside other development tools. These users need vertical or bottom layouts without sacrificing the tab-management behavior they already rely on.

The feature also introduces a language problem: commands such as “Move left” and “Close tabs to the right” become incorrect when tabs are stacked vertically. Settings must communicate both the selected orientation and the fact that it applies after relaunch.

## Goals

- Support Top, Bottom, Left, and Right tab-strip positions.
- Preserve tab, pane, workspace, drag-and-drop, color, close, and keyboard behavior.
- Make vertical layouts polished, resizable, opaque, and consistent with Terminal styling.
- Keep movement, close, tooltip, and accessibility text aligned with the visible orientation.
- Preserve the current window and terminal sessions when settings reload.

## Non-Goals

- Live template switching for an existing tab strip.
- Native-window recreation to apply the setting.
- Persisting vertical sidebar width in the settings file.
- Changing logical Forward and Backward tab-order semantics.

## Solution

Add a root/window `tabPosition` setting with `top`, `bottom`, `left`, and `right` values and a default of `top`. Root storage is required because built-in themes are omitted when the Settings UI serializes configuration.

`TerminalPage` arranges the stable tab row and terminal content according to the position captured when the window is created. Top preserves existing titlebar hosting. Bottom uses a horizontal row beneath terminal content. Left and Right use a three-column grid containing a 100–400 DIP resizable sidebar, a 4-DIP opaque resize gutter, and terminal content.

Vertical layouts apply dedicated TabView and TabViewItem templates. The templates stack items vertically, keep the tab list scrollable, place workspace controls at the top and the New Tab action at the bottom, and retain native selected, hover, pressed, foreground, close-button, and custom-color behavior.

Settings reload updates ordinary settings but deliberately leaves the active window’s tab layout unchanged. The Appearance control is labeled “Tab position (requires relaunch)” and explains that changes affect new windows after Terminal is relaunched. This avoids unsafe native-window recreation and a WinUI limitation where runtime TabView template changes collapse realized tab items.

Context-menu copy uses Left/Right and “Close tabs to the right” for horizontal strips, and Up/Down and “Close tabs below” for vertical strips. Action configuration uses orientation-neutral Forward and Backward labels because it is not bound to a particular window.

## Alternatives Considered

- **Store position in themes.** Rejected because built-in themes are intentionally omitted during Settings UI serialization, causing the setting not to persist reliably.
- **Recreate the HWND and XAML island.** Rejected because terminal controls, window callbacks, taskbar state, and native identity are bound to the existing host.
- **Switch TabView templates live.** Rejected after runtime testing showed existing tab items could not be reliably re-realized when moving between horizontal and vertical templates.
- **Use a transparent resize gutter.** Rejected because acrylic and terminal pixels visibly bleed through the divider.

## Risks & Rabbit Holes

- Cross-resource style references can compile but fail while loading XAML; the vertical close button must remain self-contained.
- Vertical TabView alignment must remain Stretch or the tab-list region collapses and the New Tab footer floats beneath the first item.
- Top plus `showTabsInTitlebar` affects native window construction and sizing.
- Focus mode, fullscreen, tab-row hiding, drag/drop indices, and launch sizing must use the selected orientation.
- Directional UI text must use the position captured by the current window, not a newly saved pending value.

## Cross-Cutting Concerns

- **Accessibility:** preserve keyboard navigation, automation names, close controls, and screen-reader movement announcements.
- **Localization:** all user-facing strings originate in `.resw` resources; new orientation-specific base strings are translatable.
- **Performance:** no timer, sleep, wait, network call, or blocking I/O is added to Settings opening.
- **Compatibility:** absent or invalid position data falls back through the existing settings-model behavior, with Top as the default.

<!-- Pipeline tracking (auto-managed, not part of product spec) -->
## Pipeline Status
Phase: CERTIFYING
State: BLOCKED
Blockers:
- TerminalApp TAEF execution fails before test code runs with RPC error `0x8007000E`.
- Direct packaged-app verification requires an unlocked, connected Windows desktop session.
- Strict repository-wide certification reports an unrelated pre-existing integrity risk in `.github/workflows/winget.yml`; expanding this feature branch to modify that workflow was declined.
