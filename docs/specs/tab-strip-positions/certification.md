# Certification Report: Configurable Tab Strip Positions

**Date:** 2026-09-03  
**Scope:** P1  
**Verdict:** NOT READY

## Completed Evidence

| Gate | Evidence | Result |
|------|----------|--------|
| Settings Model test | `te.exe SettingsModel.Unit.Tests.dll /name:*DeserializationTests::LayerGlobalProperties*` | PASS, 1 passed |
| TerminalApp test compilation | Debug x64 `TerminalApp.LocalTests.vcxproj`, bounded `/m:1` | PASS |
| Package build | Debug x64 `CascadiaPackage.wapproj`, bounded `/m:1` | PASS |
| Package deployment | `CascadiaPackage.build.appxrecipe` | PASS; `WindowsTerminalDev` registered from this worktree |
| C++ formatting | `clang-format --dry-run --Werror` on changed C++ and headers | PASS |
| XAML formatting | repository `xstyler --passive` on four changed XAML files | PASS, 4/4 |
| Structured files | Parse changed XAML, RESW, project, schema, and JSON files | PASS |
| Plan verification | `devx plan-check --strict docs\specs\tab-strip-positions\plan.md` | PASS |
| Documentation accuracy | Product, architecture, spec, test plan, and code constants/resources cross-checked | PASS |
| Feature security | STRIDE and OWASP review of the changed feature | PASS; no feature-scoped vulnerability |

## Feature Findings Resolved

- Made the vertical resize handle keyboard and automation accessible.
- Required successful primary-pointer capture and tracked the active pointer ID.
- Cleared resize state on release, cancellation, and capture loss.
- Prevented a second pointer from replacing the active resize owner.
- Collapsed and restored vertical column minimum-width constraints when tabs hide or show.
- Used effective Top-only titlebar visibility for runtime visibility and window sizing.
- Made Always Show Tabs enablement position-aware in Settings.
- Preserved custom tab colors and high-contrast resources in the vertical template.
- Bound the vertical list selection to `TabView.SelectedItem`.
- Reused the shared default tab-strip width and removed the redundant projected position API.
- Avoided duplicate layout and automation updates when the width has not meaningfully changed.
- Expanded automated coverage for all enum values, layout geometry, width bounds, opacity, visibility, accessibility state, relaunch stability, and orientation-aware text.

## Blocking Evidence

### TerminalApp runtime tests

The two tab-position TerminalApp tests compile but are blocked before test code executes:

```text
HRESULT 0x8007000E
Failed to set the authentication info on the local RPC binding.
```

The missing `Microsoft.VCLibs.140.00.Debug` dependency was installed, the test host package was rebuilt, and the tests were retried four times. Jon Gallant approved recording this machine-specific blocker, but strict certification still requires passing execution evidence.

### Direct packaged-app verification

The final package deployed successfully. Background UI verification could not start Windows Terminal Dev because the Windows desktop session was locked or disconnected.

### Repository-wide baseline security

Strict certification also rejects the unchanged `.github/workflows/winget.yml` workflow because it downloads and executes a mutable `wingetcreate/latest` artifact while holding a publishing token without checksum or signature verification. This finding is outside the tab-position feature scope. Jon Gallant chose to keep this branch scoped and not modify the workflow.

## Decision

Do not rebase, stage, commit, push, or create a pull request. The worktree remains in Phase 4 (`CERTIFYING`) with persistent implementation, test, documentation, and blocker evidence. Bare `go` can resume after the desktop and TAEF environment are available or after repository-wide certification policy can be satisfied without expanding this feature branch.
