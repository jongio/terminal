# Test Plan: Configurable Tab Strip Positions

## Status: COVERED
## Spec: docs/specs/tab-strip-positions/spec.md
## Created: 2026-09-02
## Updated: 2026-09-02

---

## Coverage Strategy

Use Settings Model unit tests for JSON compatibility and TerminalApp local tests for layout stability, orientation-aware text, accessibility state, geometry, and session preservation. Build the Debug x64 application package to compile the XAML templates and resource dictionaries. Direct packaged-app checks supplement the automated suite for native titlebar and rendered visual behavior.

## Planned Tests

| ID | Behavior to verify | Source | Level | Test file -> name | Status |
|----|--------------------|--------|-------|-------------------|--------|
| T1 | `tabPosition` deserializes all supported values and defaults to Top | FR-001, FR-002, NFR-005 | unit | `UnitTests_SettingsModel/DeserializationTests.cpp` -> `LayerGlobalProperties` | automated |
| T2 | A settings reload preserves the current window’s initial layout and tab object | FR-007, FR-008, NFR-001 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `TabPositionReloadRequiresRelaunch` | automated |
| T3 | Horizontal tabs expose Left/Right and close-to-the-right text | FR-010, NFR-003 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `TabPositionReloadRequiresRelaunch` | automated |
| T4 | Vertical tabs expose Up/Down and close-below text | FR-010, NFR-003 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `TabPositionReloadRequiresRelaunch` | automated |
| T5 | Top, Bottom, Left, and Right package layouts compile with their XAML resources | FR-003, FR-004, FR-011 | build | `CascadiaPackage.wapproj` Debug x64 build | automated |
| T6 | Vertical sidebar width remains constrained to 100–400 DIPs | FR-005 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `VerticalTabPositionCreatesSidebar` | automated |
| T7 | Divider and sidebar remain opaque in left and right positions | FR-006, NFR-002 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `VerticalTabPositionCreatesSidebar` | automated |
| T8 | Workspace and New Tab controls remain aligned vertically | FR-004, FR-007 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `VerticalTabPositionCreatesSidebar` | automated |
| T9 | Top, Bottom, Left, and Right use the expected row/column geometry | FR-003, FR-004 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `TabPositionReloadRequiresRelaunch`, `VerticalTabPositionCreatesSidebar` | automated |
| T10 | Settings copy clearly states that relaunch is required | FR-008, FR-009 | build/static | Settings Editor XAML/resource compilation | automated |
| T11 | Resize handle supports arrow keys, automation text, bounded width, and canceled pointer state | FR-005, NFR-003 | integration | `LocalTests_TerminalApp/TabTests.cpp` -> `VerticalTabPositionCreatesSidebar` | automated |

## Functionality Inventory (Phase 3 reconciliation)

| # | Functionality introduced | Location | Covered by | Status |
|---|--------------------------|----------|------------|--------|
| F1 | Root TabPosition enum, mapping, default, schema, and serialization | Settings Model and schema files | T1 | covered |
| F2 | Appearance ComboBox and relaunch copy | Settings Editor XAML/resources | T10 | covered |
| F3 | Top and Bottom horizontal layout arrangement | TerminalPage | T5, T9 | covered |
| F4 | Left and Right vertical layout arrangement | TerminalPage and vertical styles | T5, T6, T7, T8, T11 | covered |
| F5 | Position-aware window sizing and titlebar selection | TerminalWindow and AppHost | T5, T9 | covered |
| F6 | Position-aware tab hiding and drag/drop indices | TabManagement and TerminalPage | T5, T8 | covered |
| F7 | Position-aware movement and close-direction copy | Tab and resources | T3, T4 | covered |
| F8 | Stable current-window layout on settings reload | TerminalPage and TerminalWindow | T2 | covered |

## Gaps & Additions

- None identified during planning.

## Execution Notes

- The Settings Model tab-position test passes under TAEF.
- The TerminalApp local-test project, including both tab-position tests, compiles successfully.
- Local execution of the two TerminalApp tests is environment-blocked before test code runs because the packaged TAEF host fails to establish its local RPC binding with `0x8007000E`.
- The missing `Microsoft.VCLibs.140.00.Debug` framework was installed successfully before the final retries.
- On 2026-09-03, Jon Gallant explicitly approved recording this machine-specific execution blocker while retaining the automated tests and continuing certification.
- Direct packaged-app verification was attempted after successful deployment, but the Windows desktop was locked or disconnected and unavailable to the background UI automation engine.
