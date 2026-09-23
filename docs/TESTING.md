# Testing and verification

AppDeck manages other apps' windows and account data, so most of its behaviour can only be proven on a real Mac. This page lists the automated checks, the hooks that make GUI acceptance repeatable without touching real profiles, what was verified for the current release, and what is still open.

## Automated checks

| Command | Covers |
|---|---|
| `make test` | The localization check (`tools/check_i18n.py`) and seven portable policy suites built with AddressSanitizer and UndefinedBehaviorSanitizer: `core_test` (adapters, the conservative `config.toml` editor), `edge_test` (side panel geometry, visible rows, scrolling, drop places, auto-scroll, clean-at-rest edges, the 2×2 grid, allow-lists), `workspace_filter_test`, `usage_test` (reply parsing and a simulated day of limit polling), `proxy_test` (address lines, Basic credentials, CONNECT, request rewriting, SOCKS5 messages), `project_sync_test`, `automation_sync_test`. Pure C++17; this is what CI runs. |
| `make verify` | `make app` and `make test`, then: the static bundle check (`tools/verify_bundle.py`); the project adapter against an offline protocol fixture and — when `ChatGPT.app` is installed — against Codex's real `app-server` with scratch profiles; the production group synchronizer (`group_sync_test`); the macOS API smoke test (selectors, struct ABI, `sysctl` argv recovery); a real signed-out `get_usage` exchange with Claude Code when it is installed; and the regression for the 0.2.0 crash inside a separate, untrusted app bundle. |
| `tests/proxy_bridge_test.sh build/AppDeck.app` | Part of `make verify` and CI, right after the static bundle check (about 70 s). The proxy bridge (`Contents/MacOS/appdeck-proxy`) against offline loopback fixtures (`tests/proxy_fixture.py`): plain and CONNECT requests through an HTTP upstream with Basic auth and a SOCKS5 upstream with a login (names, IPv4, IPv6); failover past a dead port, a wrong login (407) and a silent upstream, for tunnels and for plain requests (a POST is never sent twice); 502 when no upstream works; `check` results (`ok`, `auth`, and `fail … protocol` when a SOCKS greeting is answered in another protocol); lifecycle (serves without a deadline until `watch` or end of stdin, exits with the watched process); loopback-only listening; 8 MiB transfers byte-exact; 50 parallel requests; no credential in argv, stdout or stderr. |
| `APPDECK_GUI_SELFTEST=1 make verify` | Additionally the side panel self-test for arm64 and x86_64 (Rosetta), in English and Russian. It shows a sandbox panel for about 20 seconds per run. |

No check signs in to an account, sends a model turn or opens a real profile's database.

## Test hooks

| Hook | Effect |
|---|---|
| `APPDECK_DATA_ROOT=/abs/path` | A completely separate manager: its own state, profiles and singleton lock. Use it for every acceptance run. |
| `APPDECK_RENDER_DIR=/abs/path` | Draw every page and the side panel into PNG files and exit. Nothing is saved, launched or locked. |
| `APPDECK_DOCK_SELFTEST=1` | Only together with `APPDECK_DATA_ROOT`: open the side panel, replay presses, clicks and drags through AppKit's own event queue, print `PASS`/`FAIL` lines and exit with the number of failures. Clicks are recorded instead of launching profiles. Data: `python3 tools/make_dock_sandbox.py <folder> 9`. |
| `APPDECK_NO_TERMINAL=1` | Write the Claude Code sign-in script but do not open Terminal. |
| `APPDECK_SYNC_ONCE=1` | One bounded group-sync pass with a JSON report, then exit. Never starts or closes a window. |
| `APPDECK_CODEX_BINARY=/path/codex` | The Codex binary for the native sync checks. |
| `-AppleLanguages '(ru)'` | Run one instance in Russian (or `'(en)'` in English) regardless of the system language. |
| `tools/make_demo_data.py` | Fictional profiles with cached limits, used for the README screenshots. |

Why the self-test replays events inside the app: on macOS 26 mouse events posted to another process (`CGEventPostToPid`) are dropped, and posting to the system event stream would move the user's pointer. The self-test therefore covers everything from `-[NSApplication sendEvent:]` on; a real pointer or trackpad gesture is part of the manual checklist.

## Status of 0.12.0

Verified on macOS 26.2, Apple Silicon, Apple clang 17:

- `APPDECK_GUI_SELFTEST=1 make verify` passed: 677 localized string pairs; core 273, edge 485 417, workspace filter 68, usage 1 469, proxy 157, project sync 44 and automation sync 80 assertions; the proxy bridge against offline fixtures (75 checks, also on x86_64 under Rosetta and once with AddressSanitizer/UndefinedBehaviorSanitizer); the static bundle check for both slices; native project import/update/delete and 188 group-sync assertions against Codex's `app-server`; API and Accessibility smoke tests; a signed-out `get_usage` exchange with Claude Code; the side panel self-test (17 checks) on arm64 and x86_64 in English and Russian.
- The redesign was checked against the design canvas in the running app with demo data (`tools/make_demo_data.py`), over a neutral wallpaper: every main-window page in English and the Claude group in Russian, the side panel in both languages, labels at the default window size with nothing truncated, the traffic lights inside the sidebar pane, no control focused when the window opens. The installed build on real data, including a window restored from the frame 0.10 saved (it fills the window with no gap under the title bar).
- `APPDECK_RENDER_DIR` renders of all pages and the side panel in both languages.
- The new icon: every size of `AppDeck.icns` rendered at its own pixel size; 16 and 32 px decode correctly through AppKit.
- Proxy end to end in a sandbox (`APPDECK_DATA_ROOT`, English and Russian): a real Codex profile launched through a local forwarding proxy with a login; every connection of the app (chatgpt.com, openai.com, Google services) went through it with the right credentials, a dead first address was skipped, the app carried the proxy switches and environment, the card showed “via …”, **Check** reported the working address, and the bridge exited with the window. An independent review of the bridge, the protocols and the integration was fixed before the release.

Not verified yet:

- A real provider proxy (HTTP or SOCKS5) and the primary window launched through a proxy (the sandbox test used an additional profile and a local proxy).
- Pointer and trackpad gestures from real hardware (drag, momentum scrolling) and the ⌃⌥ shortcuts by real key presses.
- Several different real accounts working at once with shared history; a scheduled automation firing after an owner change.
- The account used by the Code tab of an additional Claude profile; a Claude Code meter signed in to a different account than its window.
- Multi-monitor setups, Spaces and Stage Manager, Intel hardware (x86_64 ran under Rosetta), macOS 13–15.
- Developer ID signing, notarization and Gatekeeper on a downloaded build.

## Manual acceptance checklist

Use `APPDECK_DATA_ROOT` and a disposable test project. Back up `~/.codex` and existing AppDeck data first, and never share `auth.json`, global-state files or chat databases.

### Installation

- `file AppDeck.app/Contents/MacOS/AppDeck` shows both architectures; `codesign --verify --deep --strict --verbose=4 AppDeck.app` passes. Ad-hoc signing is not a publisher identity.
- A second manager instance with the same data root cannot take the singleton lock.
- The main window at its minimum size in both languages: no truncated buttons, alerts in front, dark appearance.

### Side panel

- **Side panel** hides the main window and the Dock icon; the menu bar item stays. The panel floats 12 pt off the right border of the visible frame, vertically centred, all corners rounded; at most six rows are visible and the rest scrolls.
- Slide-out and reverse close; toggling quickly during the animation leaves no invisible window intercepting clicks. Reduce Motion gives a fade, Reduce Transparency an opaque background.
- The first click on a row works while another app is active, without making the panel key; hover highlights rows and tiles.
- Secondary text stays legible over a bright and over a dark window behind the panel.
- States: active, running, hidden, stopped, failed. ⌃⌥1…8 reach places one to eight, also when scrolled out of view.
- Drag a row with a mouse and with a trackpad; scroll with a wheel and with momentum; right-click menu moves. The main window's cards, the menu bar menu and ⌃⌥ numbers follow the new order.
- Test 0, 1, 5, 6, 7 and 12 profiles; renaming or removing a profile never leaves a row pointing to a stale one.
- Monitors to the left, right, above and below, different scaling, Dock on either side, a monitor unplugged, full-screen Spaces, Stage Manager.
- Reserve a shortcut in another utility: the menu bar item stays usable. VoiceOver may reserve Control-Option combinations.

### Windows

- Without Accessibility permission (a fresh ad-hoc identity), focusing an already running profile or the original Codex from a card and from the panel must not crash (0.2.0 crashed here); launching still works and **2×2** asks once without looping.
- With permission: minimized windows, dialogs, closed windows, full screen, an app whose minimum width exceeds half the screen.
- **2×2** with one to four windows gives equal cells inside the visible frame when the apps allow it; **Restore** returns the same surviving windows to their rectangles and never addresses a relaunched process as the old window.
- **Hide** never sends terminate or kill. A click raises a window once; nothing is kept on top.
- A window closed with the red button comes back from **Show window**, a panel row or ⌃⌥N; another instance of the same bundle is never raised instead.

### Primary Codex and copies

- Connect a backup or test source; the source files are byte-identical before and after automatic operations.
- An unused profile may become the primary one; initialized accounts are never replaced.
- With the original and a copy running, clear `pid` / `started` from `state.plist` while AppDeck is closed: on start the copy is re-adopted by its `--user-data-dir` and the primary card attaches to the original, never to the copy.
- With only copies running, launching the primary card starts a separate original process.
- Quit AppDeck while a copy runs, start it again and click the copy: the existing window comes forward, no second process appears.
- Start copies one by one and check the account in each; if isolation fails, do not sign out in the unexpected window.
- A new profile contains no copied `auth.json`, cookies, SQLite, history or session files before its first sign-in.
- Shared history on: the copy's `codex` process holds `<base>/state_N.sqlite` open, no `state_*.sqlite` appears in the copy's `CODEX_HOME`, `sessions` / `archived_sessions` are links to the base, and `CODEX_SQLITE_HOME` is set only for sharing profiles.
- With real, different accounts: the thread list, projects and pins match the original; create, rename, archive and unarchive a thread in a copy and see it in the original; record how Codex treats resuming another account's thread.
- A copy with its own non-empty `sessions` keeps it. Turning sharing off removes only AppDeck's links.
- Shared `config.toml`, `AGENTS.md`, skills and rules; forced workspace restrictions survive; a multi-line or ambiguous TOML stops the preparation instead of discarding settings.
- Projects: add, rename and remove in a copy and in the primary Codex, **Sync ↻**, restart; a stale copy does not resurrect a removed project; running windows show **updates pending** until restarted.
- Automations: one owner, paused replicas; changing the owner requires stopped instances and never switches on a paused schedule.

### Account limits

- The first start asks once per group; **Not now** is remembered and nothing is started.
- After consent a card shows the plan, the account, the weekly window with the remaining share and the reset time; the panel row shows the number and a thin bar. Compare with the app's own usage screen.
- A never-started copy shows **Not signed in** and no helper process is spawned for it.
- While a probe runs there is exactly one helper child of AppDeck and no grandchildren; it exits within seconds; `auth.json` is untouched; nothing is left after quitting AppDeck.
- **Limits ↻** probes immediately; a second press within 20 seconds does not. Automatic probes are at least five minutes apart and at most once an hour per account; launching, closing or signing in starts no probe by itself.
- Signing out inside a copy turns its card to **Not signed in**; switching the feature off removes the `usage` records from `state.plist`.
- Without network the last numbers stay, grey, with “data from …”; no dialogs and no busy loop.
- Claude: signed-out meters show “Claude Code sign-in needed…”; **Sign in…** writes a 0700 script with that profile's `CLAUDE_CONFIG_DIR` (use `APPDECK_NO_TERMINAL=1`); after a real sign-in the card shows week, 5 hours and the per-model window with the same numbers as Claude's own usage screen (AppDeck shows what is left); while probing no hook, MCP server or plugin starts and nothing is written to `~/.claude`.
- Two profiles signed in to one account get the ⚠ mark.
- Intel: centred captions (panel numbers, action tiles, plan pill) are centred, not right-aligned.

### Claude Desktop

- Adding `Claude.app` preselects the Claude Desktop adapter; a copy runs as a second process with `CLAUDE_USER_DATA_DIR` and `--user-data-dir` in its profile, opens no file of `~/Library/Application Support/Claude`, and can be closed and restarted; the original keeps running.
- The primary card attaches to the running original and never adopts a copy.

## Release gate

A production release needs the GUI, permission and multi-account checks above on Apple Silicon and Intel, on the oldest supported and the current macOS, Apple `codesign` verification, Developer ID signing and notarization, and a compatibility record per target app version. Passing unit tests alone does not meet this gate.
