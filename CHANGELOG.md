# Changelog

All notable changes to AppDeck. Verification details for the current release are in [docs/TESTING.md](docs/TESTING.md).

## 0.10.0

### Added
- English and Russian interface. AppDeck follows the macOS language order (Russian when it is preferred over English, or chosen for AppDeck in System Settings); AppKit's own panels use the same language. Every visible string is an English/Russian pair checked by `tools/check_i18n.py`.
- Built-in user guide rendered from `docs/USER_GUIDE.md`.
- `make` targets: `app`, `test`, `verify`, `install`, `clean`; GitHub Actions CI; demo data for screenshots.

### Changed
- Repository layout for publishing: sources at the root (`src/`, `tests/`, `tools/`, `assets/`, `docs/`, `scripts/`), build output in `build/`, no bundled binaries or archives. The Linux cross-build tooling was removed; builds use the system clang and `codesign`.
- The “right panel” is called the side panel in the English interface.
- Limit reset times use the interface language's date format.

## 0.9.0

### Changed
- The side panel shows at most six profile rows; further profiles scroll inside the list instead of being paged. Fewer rows are visible when six do not fit on the screen.
- The list is clean at rest: it opens at the top without shading, and a soft edge appears only while a row is cut by the top or bottom edge during scrolling.

### Added
- Your own profile order. Drag a panel row up or down (the list auto-scrolls near its edges) or use **Move up / Move down / Move to top** in the row's menu; a card's **•••** menu moves it among its app's cards. The same order drives ⌃⌥1…8, the menu bar menu, the main window and **2×2**. The first 0.9 launch keeps the previous app-grouped order.
- `APPDECK_DOCK_SELFTEST` for sandboxed GUI acceptance of the side panel.

## 0.8.0

### Added
- Two-way reconciliation of local Codex projects between the base and linked copies through a versioned group journal with per-profile baselines, revisions and deletion tombstones. A stale copy cannot resurrect a removed project.
- Shared automation definitions with one explicit owner and paused replicas; changing the owner requires stopped instances and never switches on a paused schedule.

### Changed
- The periodic pass only records changes in AppDeck's journal; files are applied before a full launch, on **Sync ↻** and on explicit project/owner actions, and only to stopped instances, through Codex's own app-server API plus allow-listed cache fields.

## 0.7.2

### Changed
- One polling rule for Codex and Claude: an account at most once per hour and at least five minutes between any two automatic probes; no event-driven automatic probes. Manual refresh stays immediate (20 s floor per account). A window whose reset time has passed is shown grey until it is checked again.

## 0.7.1

### Fixed
- Claude limits stayed empty after sign-in: the probe set `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC`, which makes Claude Code skip the usage request. Windows are now read from the server's `limits[]` list first.

## 0.7.0

### Added
- Claude limits: weekly (all models), 5-hour and per-model weekly windows, plan and account, through Claude Code's own `get_usage` request in `--safe-mode` against a per-profile configuration folder the user signs in to once.

## 0.6.0

### Added
- Claude Desktop adapter with per-profile `CLAUDE_USER_DATA_DIR`; primary cards for non-Codex apps.
- The side panel lists the profiles of every app; global shortcuts ⌃⌥1…8.

### Fixed
- A window closed with the red button can be brought back: focus sends the per-process *reopen* event, like a Dock click.

## 0.5.0

### Added
- **Close**, **Restart** and **Window** on running cards; the same actions in the side panel's row menu; **↻ Limits** in its header.

### Fixed
- Copies showed fewer projects than the original Codex: the modern `local-projects` registry is now carried over (superseded by the 0.8.0 reconciliation).
- A stop requested within seconds of a launch was reported as a failed start.

## 0.4.0

### Added
- Account limits for Codex profiles through the bundle's own `codex app-server` (`account/read`, `account/rateLimits/read`), opt-in per group; a warning when two profiles use the same account.

### Fixed
- Centred captions were right-aligned on Intel Macs (`NSTextAlignment` differs between arm64 and x86_64).

## 0.3.0

### Fixed
- A crash on every focus of an already running window while AppDeck had no Accessibility permission (`AXIsProcessTrustedWithOptions` called with an incomplete dictionary).
- Global shortcuts fired on key release; alerts could open behind other apps; the saved window frame was discarded; a copy AppDeck lost track of could be started twice.

### Added
- Opt-in shared history for linked Codex copies through Codex's own `CODEX_SQLITE_HOME`; re-adoption of copies by their `--user-data-dir` argument; `APPDECK_DATA_ROOT` and `APPDECK_RENDER_DIR` for acceptance work.

## 0.2.0

### Added
- The side panel, global shortcuts, the 2×2 grid with restore, and the connection to the original Codex workspace.
