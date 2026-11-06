# Changelog

All notable changes to AppDeck. Verification details for the current release are in [docs/TESTING.md](docs/TESTING.md).

## 0.15.0

### Added

- An interface-language selector in the AppDeck menu, menu-bar status menu and About page: system default, English or Russian. An explicit choice overrides the macOS language, applies immediately to AppDeck's interface, menus, side panel and date captions, and is saved for the next launch. Managed profile windows do not need to restart.

### Changed

- The language preference is stored in AppDeck's own `state.plist`. Missing or unsupported values use the system choice; profile names and stored messages are not rewritten. Native macOS file-dialog controls continue following the macOS application-language setting; documentation and bundled guides remain English.

## 0.14.0

### Added

- Newly created clean Claude profiles can use empty private Code configuration and transcript storage in `Profiles/<id>/claude-code`, while preserving the real macOS home for login Keychain compatibility. The choice is recorded separately from the existing Desktop-clean mode; older profiles are not migrated or cleared.
- Claude profile creation/settings include saved-proxy selection, required-proxy launch, optional linked-Chrome DNS hardening, five Code optional-traffic switches and a linked empty Chrome profile for Claude Web. Private Code user settings receive only the safe network allowlist, with preserved unrelated fields and an atomic backup. Proxy credentials remain in the existing secure proxy store.
- Bundled network setup guide explains HTTP CONNECT through a Tailscale VPS, the scope of proxy/DNS/WebRTC controls and why hosts entries do not provide a complete egress firewall. Native fixtures cover linked-browser configuration and bridge lifecycle on both architectures.

### Changed

- The profile-isolation rule now permits an AppDeck-selected `CLAUDE_CONFIG_DIR` only for a newly created, explicitly selected clean profile with private Code storage. Standard and primary profiles retain the usual Code environment. No source settings, transcripts or authentication are copied.
- A failed proxy handoff now cancels launch and requests normal termination of the new process instead of continuing with a stopped bridge. Required-proxy usage probes also fail without substituting a direct connection.

## 0.13.0

### Added

- Claude profile creation now offers **Clean Desktop profile (no shared sessions)** by default, with **Profile with shared Code sessions** as an explicit alternative. Clean profiles start with empty Desktop data and private temporary storage, remain outside the session bridge, and can launch alongside another copy. Both modes retain the macOS user's normal Code settings and transcripts in `~/.claude`. Existing profiles retain their previous mode; duplication opens the same choice.
- **Add app** accepts any installed macOS `.app`, including AnyDesk. Apps without a profile adapter get one launch card using their existing data, with no profile folders. Supported apps also offer **Ordinary launch (no profiles)**.
- Ordinary app cards offer an optional proxy and **Copy address** for applications that need proxy setup in their own settings. The address belongs to the local bridge and changes on each launch. Native apps receive proxy environment settings without Chromium switches; routing depends on the app and is not verified.
- Opt-in local Claude Code session bridge: a shared catalogue grouped by project, **Open in…** account selection, Claude-owned registration through a pid-addressed native resume event, and registration evidence from target descriptors rather than an assumed success after delivery. Persistent process reservations conservatively prevent a different shared-Code profile launch through AppDeck while the shared conversation is open. Clean profiles neither publish nor receive bridged sessions and do not inherit that reservation. Cloud Chat, Cowork and schedules remain separate.
- Portable bridge policies and offline macOS production scanner/reservation/event-construction fixtures for both architectures. No vendor executable or GUI is needed for these fixtures.

### Fixed

- Claude launches preserve the real macOS home so Security can use the normal login Keychain. Changing `HOME` prevented a fresh Desktop window from opening. Clean mode separates Desktop data; it does not isolate the global Code environment or reset macOS permissions.
- **Launch** no longer mistakes AnyDesk's background service for its application window. AppDeck selects the main application process, starts it when only background workers exist, and keeps multiple eligible application instances ambiguous. Native processes without a LaunchServices launch date use the public kernel birth timestamp for identity; an unavailable identity now reports a launch error instead of returning silently.
- Corrected the claim that sharing `~/.claude` shares Claude Desktop's session lists. The native Desktop registry is separate and account-scoped. Additional Claude profiles launch the unchanged app executable with an exact environment, removing inherited Claude/Anthropic and Codex/OpenAI overrides, including `CLAUDE_CONFIG_DIR`. This prevents LaunchServices from restoring inherited values or an empty override from redirecting Code configuration. AppDeck does not choose a Desktop `CLAUDE_CONFIG_DIR`.
- Native resume is gated to inspected Claude Desktop 2.31226.1; unknown versions remain available for ordinary launches but do not receive an import request. Real multi-account import, Automation permission and visual acceptance remain unverified; see the verification status.

## 0.12.1

### Fixed
- ChatGPT 26.924 moved Codex's helper from `Contents/Resources/codex` to `Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex`. AppDeck found no helper there, so every launch with shared projects stopped with "No compatible app-server for project sync was found in the app" and account limits showed no numbers. AppDeck now uses the new location, the one the desktop app itself starts, and still finds the old one in earlier builds. The diagnostics name the helper that was found.

## 0.12.0

### Added
- Proxy support. A **Proxy** page lists HTTP (CONNECT) and SOCKS5 proxies with one or more addresses in the provider's `host:port[:login:password]` form, a common login, a **Check** that opens a tunnel to `api.openai.com:443` through every address, and the default proxy for all profiles; a card's **•••** menu picks a profile's own (default, none or a specific proxy) and the status line shows it. A profile behind a proxy starts with a private loopback bridge (`appdeck-proxy`) that adds the credentials, fails over along the address list and lives as long as the window; the app gets `--proxy-server`, `--disable-quic`, a WebRTC policy that keeps UDP inside the proxy and the `*_PROXY` environment. The primary window (your usual Codex or Claude) uses it too when AppDeck starts it. Limit checks use the same proxy. Passwords are kept in `proxies.plist` (0600), never in `state.plist` or the diagnostics; a `proxies.plist` that cannot be read is never overwritten, and profiles that use a proxy from it are not started without it.

### Changed
- Adding an app no longer asks how many profiles to create and no longer calls Codex experimental: add profiles one by one with **New profile**.

## 0.11.0

### Changed
- New visual design, "Graphite glass": the main window is one behind-window blur with a floating sidebar pane; one bone-white ink, capsule controls of one height, colour only for state (running, low, critical) and for each profile's badge. Every page shares one content column and one header, body and footer grid.
- Profile cards: the app icon carries the profile's colour badge; plan and ⌘-number sit beside the title; the facts line up with the limit columns; the main action is a glass capsule (**Launch** or **Show window**), the others are round icon buttons with tooltips (Folder, Restart, Close, More). A failed start shows a warning button instead of a text link.
- Limits: what is left in each window is a thin capsule meter in neutral ink that turns amber below 25 % and red below 10 %; with three windows (Claude) a card shows the numbers alone.
- Shared settings, Projects and About are grouped lists with equal trailing buttons; page actions sit on the title line or in the footer.
- The side panel floats 12 pt off the screen edge with all corners rounded; rows show a status light and the limit on the status line; the four tools form one toolbar.
- New app icon: the side panel on graphite glass.
- The profile colours are sky, sage, sand, clay, rose and teal (clay replaces violet).
- The minimum window width is 1120 pt so that Russian labels fit.
- README and user guide rewritten for the new interface, with new screenshots of the main window, the side panel, Shared settings and the Russian interface.

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
