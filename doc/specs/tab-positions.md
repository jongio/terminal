---
author: Jon Gallant
created on: 2026-09-03
last updated: 2026-09-03
issue id: #835
---

# Configurable tab strip positions

## Abstract

This spec adds a `tabPosition` window setting that places the tab strip at the
top, bottom, left, or right edge of a Terminal window. Top remains the default,
so existing settings and window layouts keep their current behavior.

Changing the setting applies to newly created windows after Terminal is
relaunched. Existing windows keep their original tab layout and terminal
sessions.

## Background

### User stories

- A user with many tabs can place them in a vertical list so more titles remain
  visible.
- A user with limited vertical space can place tabs on the left, right, or
  bottom.
- A user can resize a vertical tab strip without exposing terminal content
  through its divider.
- Keyboard and screen-reader users receive controls and directional text that
  match the selected orientation.

### Relaunch requirement

The tab position is captured while a window is created. Runtime switching
between horizontal and vertical WinUI `TabView` templates was tested, but the
existing tab items did not reliably re-realize after the template changed.

Recreating the native window was also rejected. The XAML island, terminal
controls, window callbacks, taskbar state, and native window identity are tied
to the existing host. Keeping the current window unchanged avoids risking
active sessions.

## Solution design

### Setting

`tabPosition` is a root window setting:

```json
{
    "tabPosition": "left"
}
```

Supported values:

| Value | Behavior |
|-------|----------|
| `top` | Horizontal tab strip above terminal content. This is the default. |
| `bottom` | Horizontal tab strip below terminal content. |
| `left` | Vertical tab strip to the left of terminal content. |
| `right` | Vertical tab strip to the right of terminal content. |

The setting is stored at the root rather than in a theme. Built-in themes are
not serialized by the Settings UI, while the tab position is a window layout
choice that must persist independently of the active theme.

The Appearance page exposes the four values and identifies the setting as
requiring a relaunch.

### Window and titlebar behavior

`TerminalWindow` caches the initial value before calculating launch dimensions.
`AppHost` uses non-client titlebar hosting only when the position is `top` and
tabs in the titlebar are enabled.

Bottom, left, and right positions remain in the client area. Launch and resize
calculations reserve tab-strip space only when the strip is effectively
visible.

### Horizontal layouts

Top uses the existing tab row and titlebar behavior. Bottom moves the same tab
row below terminal content. The implementation reuses the existing
`TabRowControl`, tab collection, and terminal content rather than creating
position-specific copies.

### Vertical layouts

Left and right positions use a three-column layout:

1. A vertical `TabRowControl`.
2. An opaque resize gutter.
3. Terminal content and info bars.

The order is reversed for the right position. Deferred overlays such as the
command palette, suggestions, dialogs, teaching tips, and info bars continue to
span the full window.

The vertical strip:

- Defaults to 200 DIPs wide.
- Can be resized between 100 and 400 DIPs.
- Uses a 4-DIP opaque layout gutter with a centered 1-DIP visual divider.
- Uses a 12-DIP input target that overlaps both sides of the gutter.
- Restores its previous width after the tab strip is hidden and shown.
- Places workspace controls at the top and New Tab at the bottom.

`VerticalTabViewStyle.xaml` supplies vertical `TabView` and `TabViewItem`
templates. The list binds to the `TabView` selection and reuses existing
per-tab theme resources so custom colors and high-contrast states continue to
work.

### Tab actions and text

Tab ordering remains logical rather than position-specific:

- Horizontal strips display **Move left**, **Move right**, and
  **Close tabs to the right**.
- Vertical strips display **Move up**, **Move down**, and
  **Close tabs below**.
- Generic action configuration uses **Forward** and **Backward** because the
  action can run in a window with any orientation.

## UI and accessibility

The vertical resize handle is keyboard focusable and exposes a localized
automation name, help text, control type, and current width. Left and Right
Arrow keys resize the strip in 10-DIP increments.

Pointer resizing begins only after primary-pointer capture succeeds. The handle
tracks the initiating pointer and clears its drag state on release,
cancellation, or capture loss.

The vertical tab templates retain system focus visuals, selected and hover
states, close buttons, custom tab colors, and high-contrast resources.

## Compatibility

- Missing `tabPosition` values default to `top`.
- Existing settings files remain valid.
- Existing windows do not move their tab strips during settings reload.
- The feature does not recreate tabs, panes, terminal controls, or native
  windows.
- Sidebar width is not persisted to JSON.

## Validation

Automated coverage includes:

- Deserialization of all four values and the omitted-value default.
- Preservation of the current tab and layout during settings reload.
- Horizontal and vertical tab-action wording.
- Left, right, and bottom layout geometry.
- Vertical width limits, visibility restoration, divider opacity, and
  automation state.

The Settings Model test passes. The TerminalApp local-test project and complete
Debug x64 package build successfully. Local execution of the packaged
TerminalApp tests is currently blocked before test code runs by a TAEF host RPC
failure (`0x8007000E`).

## Potential issues

- The vertical templates must be reviewed when the repository updates its
  WinUI `TabView` version.
- Changes to tab-row height, default sidebar width, or resize-gutter width must
  also update launch and window-sizing calculations.
- Focus mode, fullscreen mode, hidden tabs, overlays, and cross-window tab drag
  must continue to use the position captured for the current window.

## Future considerations

- Live position changes can be reconsidered if WinUI supports reliable
  horizontal-to-vertical template transitions for realized tab items.
- Persisting the resized vertical width can be considered separately if users
  need it across windows.

## Resources

- [Tab position feature request][issue-835]

[issue-835]: https://github.com/microsoft/terminal/issues/835
