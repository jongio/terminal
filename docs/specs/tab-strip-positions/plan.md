# Development Plan: Configurable Tab Strip Positions

## Overview

Add a root Windows Terminal setting that places the tab strip on the top, bottom, left, or right. Preserve existing tab and pane behavior, provide polished vertical layouts, and require relaunch rather than rebuilding an active window.

## Convention Discovery

- Settings are declared in `TerminalSettingsModel`, mapped to JSON through existing macros, and documented in `doc/cascadia/profiles.schema.json`.
- Settings Editor enum choices use `EnumEntry`, `.resw` localization, and `x:Bind`.
- Window construction decisions that affect the native titlebar are made in `TerminalWindow` and `AppHost`.
- Page layout and tab behavior remain in `TerminalPage`, `TabManagement`, and `Tab`.
- C++ tests use TAEF; XAML is formatted with the repository `xstyler` tool.

## Issues

### 1. Add the tab-position settings contract

- **Type:** settings | **Size:** M | **Dependencies:** none
- **Context:** Add the enum, default, JSON values, schema, Settings UI, and relaunch copy.
- **Acceptance Criteria:**
  - [x] `tabPosition` accepts `top`, `bottom`, `left`, and `right`.
  - [x] Missing configuration defaults to `top`.
  - [x] Settings UI exposes all four localized choices.
  - [x] Settings UI states that changes require relaunch.

### 2. Compose horizontal and vertical layouts

- **Type:** frontend | **Size:** L | **Dependencies:** Issue 1
- **Context:** Arrange existing tab, content, info-bar, and overlay surfaces without duplicating tab state.
- **Acceptance Criteria:**
  - [x] Top and Bottom use horizontal tab rows.
  - [x] Left and Right use vertical sidebars.
  - [x] Vertical sidebars resize from 100 through 400 DIPs.
  - [x] The 4-DIP opaque gutter contains a 1-DIP visual divider and a 12-DIP overlapping input target.
  - [x] Hiding vertical tabs collapses the column minimum and restores it when shown.
  - [x] The vertical list synchronizes selection with `TabView.SelectedItem`.

### 3. Preserve lifecycle and interaction behavior

- **Type:** integration | **Size:** L | **Dependencies:** Issues 1-2
- **Context:** Align titlebar hosting, sizing, settings reload, drag behavior, accessibility, and orientation-aware language.
- **Acceptance Criteria:**
  - [x] Only top-positioned tabs can use native titlebar hosting.
  - [x] Launch and resize calculations reserve tab chrome only when tabs are effectively visible.
  - [x] Existing windows preserve their initial orientation after settings reload.
  - [x] Horizontal tab actions use Left/Right language and vertical actions use Up/Down language.
  - [x] The resize handle supports primary-pointer capture, pointer ownership, cancellation, arrow keys, and localized automation text.
  - [x] Existing custom tab colors and high-contrast resources flow through the vertical template.

### 4. Verify and document the feature

- **Type:** testing | **Size:** M | **Dependencies:** Issues 1-3
- **Context:** Add settings and TerminalApp tests, compile the package, and record architecture and product decisions.
- **Acceptance Criteria:**
  - [x] Settings Model tests cover every enum value and the omitted default.
  - [x] TerminalApp tests cover relaunch stability, orientation text, layout geometry, width bounds, opacity, visibility, and accessibility state.
  - [x] The TerminalApp local-test project compiles.
  - [x] The Debug x64 package builds and deploys.
  - [x] Product requirements, architecture, specification, board review, and test plan are written without unresolved markers.

## Impact Scan

| Surface | Impact |
|---------|--------|
| Settings JSON and schema | New backward-compatible root enum with Top default |
| Settings UI | New ComboBox, relaunch guidance, position-aware Always Show Tabs enablement |
| Native window | Top-only non-client titlebar hosting and corrected launch sizing |
| Terminal page | Four layout compositions, vertical resize handle, overlay spanning |
| Tab presentation | Dedicated vertical TabView and TabViewItem templates |
| Accessibility | Keyboard resizing, automation name/help/status, high-contrast resource reuse |
| Localization | New position, direction, close-after, relaunch, and resize-handle strings |
| Tests | Expanded Settings Model and TerminalApp local coverage |
| Dependencies | No new runtime dependency |
| Security/privacy | No new network, identity, secret, telemetry, or personal-data surface |

## Acceptance Criteria

- [x] Users can select Top, Bottom, Left, or Right and the value persists in root settings.
- [x] New windows render the selected horizontal or vertical layout after relaunch.
- [x] Existing windows and terminal sessions remain stable when settings reload.
- [x] Vertical sidebars remain opaque, resizable, keyboard accessible, and bounded to 100-400 DIPs.
- [x] Tab selection, visibility, custom colors, high contrast, and directional text follow the selected orientation.
- [x] Native titlebar hosting and launch sizing use the effective Top-only titlebar state.
- [x] Schema, localization, Settings UI, automated tests, and feature documentation are complete.

## Quality Gates

- `MSBuild.exe src\cascadia\LocalTests_TerminalApp\TerminalApp.LocalTests.vcxproj /m:1 /p:Configuration=Debug /p:Platform=x64` exits 0.
- `MSBuild.exe src\cascadia\CascadiaPackage\CascadiaPackage.wapproj /m:1 /p:Configuration=Debug /p:Platform=x64 /p:GenerateAppxPackageOnBuild=false` exits 0.
- `te.exe SettingsModel.Unit.Tests.dll /name:*DeserializationTests::LayerGlobalProperties*` passes.
- Changed C++ files pass `clang-format --dry-run --Werror`.
- Changed XAML files pass `dotnet tool run xstyler -- --passive`.
- Changed XML and JSON files parse successfully.
- The test plan maps every functionality inventory row to automation and has no gaps.
- Full quality certification has zero feature-scoped Critical or High findings.

## Pre-Completion Interview

All product decisions are resolved:

- The setting applies after relaunch.
- Active windows and sessions are not recreated.
- Sidebar width is not persisted.
- Top remains the compatibility default.
- Directional text follows visible orientation.
- The local TAEF host execution blocker is explicitly approved and documented.

## Gut-Check Results

- **Value:** Solves a demonstrated customization need for wide displays and high tab counts.
- **Feasibility:** Uses existing settings, window, tab, resource, and test infrastructure.
- **Complexity:** Custom vertical templates are justified because the stock control is horizontal.
- **Risk:** Bounded through launch-time application, typed enum parsing, focused tests, and package builds.
- **Alternatives:** Live switching and native-window recreation were tested or analyzed and rejected for reliability.

## Done Definition

- [x] Every acceptance criterion is implemented.
- [x] Every planned test exists as automation.
- [x] Settings Model test passes.
- [x] TerminalApp test and package projects build.
- [x] Formatting and structured-file checks pass.
- [x] Documentation matches the implementation.
- [ ] Full certification reports READY.
- [ ] Human approves rebase, staging, commit, and push.
- [ ] Human separately approves pull-request creation.

## Suggested Sequence

1. Add the settings contract and editor surface.
2. Compose horizontal and vertical layouts.
3. Integrate titlebar, sizing, interaction, and accessibility behavior.
4. Add tests and documentation.
5. Certify, rebase, commit, push, and create the pull request after explicit approval.
