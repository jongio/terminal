# Product Requirements: Configurable Tab Strip Positions

**Author:** Jon Gallant | **Date:** 2026-09-02 | **Status:** Approved

---

## Problem Statement

Windows Terminal users with wide displays, vertical workflows, or limited vertical space cannot choose where the tab strip appears. The fixed top layout wastes usable space for some workflows and makes tab management inconsistent with applications that support vertical tabs. Without a configurable position, users must accept a layout that may not fit their screen or accessibility preferences.

## Goals

1. Let users place the tab strip at the top, bottom, left, or right edge of each new Terminal window.
2. Preserve existing tabs, panes, workspaces, drag-and-drop, close actions, colors, and keyboard behavior in every position.
3. Provide a polished vertical layout with an opaque, resizable divider and controls aligned to the tab list.
4. Make movement and close-direction text describe the visible tab orientation.
5. Communicate clearly that changing tab position requires relaunching Windows Terminal.

## User Personas

**Morgan — Power user.** Uses many concurrent terminal tabs on an ultrawide display and wants a vertical tab list that exposes more titles.

**Riley — Developer.** Uses Terminal beside an editor and wants bottom or side tabs to match the rest of the workspace.

**Casey — Keyboard and assistive-technology user.** Needs tab commands, automation text, and focus behavior to remain accurate regardless of tab orientation.

## Use Cases

1. **UC-001: Select a tab position.** The user opens Appearance settings, selects Top, Bottom, Left, or Right, saves, and relaunches Terminal. New windows use the selected edge.
2. **UC-002: Resize a vertical tab strip.** The user drags the opaque divider between the tab strip and terminal content. The strip remains between 100 and 400 DIPs and preserves its width while hidden or moved between left and right.
3. **UC-003: Manage tabs in any orientation.** The user creates, selects, closes, reorders, drags, colors, renames, duplicates, splits, or moves tabs without losing existing behavior.
4. **UC-004: Interpret directional actions.** Horizontal layouts show Move left/Move right and Close tabs to the right. Vertical layouts show Move up/Move down and Close tabs below.
5. **UC-005: Change the setting in an open window.** The active window keeps its original layout and sessions. The settings UI explains that relaunch is required.

## Functional Requirements

| ID | Requirement | Priority | Notes |
|----|-------------|----------|-------|
| FR-001 | Expose Top, Bottom, Left, and Right tab positions in Appearance settings | Must | Root `tabPosition` setting |
| FR-002 | Serialize positions as `top`, `bottom`, `left`, and `right` | Must | Default is `top` |
| FR-003 | Render top and bottom positions as horizontal tab rows | Must | Top retains titlebar hosting behavior |
| FR-004 | Render left and right positions as vertical sidebars | Must | Sidebar includes workspace and New Tab controls |
| FR-005 | Allow vertical sidebars to resize between 100 and 400 DIPs | Must | Default width is 200 DIPs |
| FR-006 | Use an opaque 4-DIP resize gutter with a 1-DIP hover indicator | Must | Prevent terminal/acrylic bleed-through |
| FR-007 | Preserve tab, pane, workspace, drag, reorder, close, and color behavior | Must | No session recreation |
| FR-008 | Keep the current window layout stable after settings reload | Must | Relaunch required |
| FR-009 | Display the relaunch requirement beside the setting | Must | Mirrors established settings wording |
| FR-010 | Adapt tab movement and close-direction text to orientation | Must | Up/down vertically, left/right horizontally |
| FR-011 | Publish schema and localized base resources for the setting | Must | Translation pipeline consumes en-US source |

## Non-Functional Requirements

| ID | Requirement | Target | Notes |
|----|-------------|--------|-------|
| NFR-001 | Existing terminal sessions survive settings reload | 100% | Current window is not rebuilt |
| NFR-002 | Vertical divider is visually opaque | No background pixels visible | All themes |
| NFR-003 | Tab controls remain keyboard and automation accessible | Existing accessibility behavior retained | Directional labels remain accurate |
| NFR-004 | Settings-open performance regression | No explicit delay or blocking I/O added | Debug-build overhead is not product behavior |
| NFR-005 | Configuration compatibility | Missing setting behaves exactly as Top | Existing settings files remain valid |

## Success Metrics

| Metric | Baseline | Target | Measurement |
|--------|----------|--------|-------------|
| Available tab positions | 1 | 4 | Settings model and UI enumeration |
| Supported layout transitions on relaunch | Top only | Top, Bottom, Left, Right | Local UI verification |
| Directionally incorrect context labels | Present for vertical tabs | 0 | Resource and UI checks |
| Existing tab/session loss on settings reload | Not applicable | 0 | TerminalApp local test |

## Out of Scope

- Live switching of an existing TabView between horizontal and vertical templates; WinUI does not reliably re-realize existing tab items.
- Recreating the native window or terminal sessions to simulate live switching.
- Persisting a separate sidebar width setting in JSON.
- Changing tab ordering semantics; Forward and Backward retain their existing logical meaning.
