# Testing and verification

AppDeck manages other apps' windows and account data, so most of its behaviour can only be proven on a real Mac. This page lists the automated checks, the hooks that make GUI acceptance repeatable without touching real profiles, what was verified for the current release, and what is still open.

## Automated checks

| Command | Covers |
|---|---|
| `make test` | The localization check and eight portable ASan/UBSan policy suites: core, edge, workspace filter, usage, proxy, project sync, automation sync and Claude session policy (metadata allowlist, version gate, path/URL validation, capacity bounds and ownership admission). Pure C++17; this is what CI runs. |
| `make test-claude-sessions` | Offline macOS fixtures on arm64 and x86_64/Rosetta with ASan/UBSan: the production Claude metadata scanner, sanitized catalogue, conflicts, native-registration evidence, durable reservations, malformed input and link/size/budget protection. Constructs pid-addressed Apple events without sending them. Does not run NSApplication, Claude, any vendor helper or any real profile. |
| `make test-claude-profiles` | Production Claude launch and storage preparation against disposable native fixture applications on arm64 and x86_64/Rosetta, with ASan/UBSan. Checks private Code storage, legacy/shared modes, exact environment and working directory, concurrent child identities, restart/adoption, persisted mode, bridge exclusion, safe settings reconciliation and private-directory protections. Does not launch real Claude or use a real account. |
| `make test-claude-network` | Production linked-Chrome creation/reuse, proxy selection, separate real loopback bridges, credential-free child arguments/environment/private settings, required-proxy failures, persistence and bridge lifecycle on both architectures with ASan/UBSan. Uses only native fixture apps and a synthetic upstream; sends no network request to a vendor or real proxy. |
| `make verify` | `make app`, `make test`, the offline Claude session fixtures, native clean-profile and network fixtures, then: the static bundle check (`tools/verify_bundle.py`); native ordinary-app launch/adoption and proxy-lifecycle fixtures on both architectures; the project adapter against an offline protocol fixture and — when `ChatGPT.app` is installed — against Codex's real `app-server` with scratch profiles; the production group synchronizer (`group_sync_test`); the macOS API smoke test (selectors, struct ABI, `sysctl` argv recovery); a real signed-out `get_usage` exchange with Claude Code when it is installed; and the regression for the 0.2.0 crash inside a separate, untrusted app bundle. |
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

## Status of 0.15.0

AppDeck now saves its own interface-language preference and switches the custom UI immediately between system default, English and Russian. The choice is available from the AppDeck menu, menu-bar status menu and About page. It does not restart managed applications or rewrite profile names, and native macOS file-dialog controls keep their platform language.

Verified on macOS 26.2 / Apple Silicon:

- A disposable native GUI harness passed 112 assertions on arm64 and 112 on x86_64/Rosetta. It dispatched the actual About popup and menu actions, checked translated main/status menus, the status-button tooltip and visible side panel, saved/reloaded the preference, restored either system-language choice, rejected invalid choices and rolled back a failed settings write. Profile contents, order and selection were preserved. No managed application or real profile was used. Evidence: `build/language-qa/{arm64,x86_64}/report.json`.
- The built application was started in separate scratch render processes with saved Russian against `-AppleLanguages '(en)'`, and saved English against `-AppleLanguages '(ru)'`. Both rendered the saved language and left their input state unchanged. Evidence: `build/language-render-0.15.0/startup-render-report.json`.
- Actual English/Russian About-window and side-panel screenshots were reviewed for both architectures at the minimum 1120 x 720 window size; the new selector fits without overlap. The app-owned interface switches live. Native macOS file-dialog controls and previously stored text retain their own language.

`APPDECK_GUI_SELFTEST=1 make verify` passed (exit 0; `build/final-verification-0.15.0.log`): 840 localized string pairs, all eight portable ASan/UBSan suites, 99 Claude session, 360 clean-profile, 131 network and 142 ordinary-app assertions per architecture, 76 offline proxy checks, native project fixtures and 190 group-sync assertions, signed-out safe-mode Claude Code `get_usage`, API/AX smoke tests, and 17 side-panel checks for each architecture/language combination. The universal warnings-as-errors build and static bundle checks passed. The release-candidate ZIP was extracted and verified with system `codesign --verify --deep --strict`, both architecture checks and a byte comparison with the built bundle (`build/package-validation-0.15.0.json`). Developer ID signing, notarization, Intel hardware and macOS 13–15 remain untested.

The saved proxy was updated after the 0.14.0 release checks below. A separate recheck established successful proxy authentication and verified HTTPS CONNECT/TLS to `claude.ai` and `api.anthropic.com`; two external-address samples matched. A new disposable Claude Desktop instance through that proxy rendered the first-run welcome and Cookie settings dialog. It quit normally, all owned helper/scratch data was removed, and the original Claude stayed running. No sign-in or model request was performed. Evidence: `build/proxy-recheck-updated.json` and `build/claude-proxy-recheck-2026-11-06.json`. This does not establish complete network confinement or indefinite IP stability.

## Previous status of 0.14.0

This release adds private Code storage for newly created clean Claude profiles while preserving the real macOS home. The new `claudeCodeIsolated` setting is separate from the existing `claudeClean` flag: older clean profiles remain Desktop-only, and standard/primary profiles keep their usual Code environment.

Verified on macOS 26.2 / Apple Silicon with installed Claude Desktop 4.4212.0 and Claude Code 2.1.241:

- Private-profile native fixtures: 360 assertions each on arm64 and x86_64/Rosetta with ASan/UBSan. Checks independent Code roots/markers, 0700 directories, exact environment, preserved native `HOME`, unchanged synthetic source settings, legacy/shared compatibility, relaunch/adoption/reload, five traffic opt-outs, required-proxy failures and safe settings merge/backup. Malformed JSON, symlinks, hard links, oversized files and changed input are rejected.
- Network fixtures: 131 assertions per architecture with real bridge processes and disposable native stand-ins. Checks separate linked-browser storage, route synchronization, proxy/QUIC/WebRTC arguments, Chromium-only resolver hardening, Code aliases/settings, no upstream credentials in arguments/environment/state, bridge lifetime, persistence and missing/invalid required-proxy failures. No upstream requests are made in this suite.
- Real no-proxy baseline: two new Code-isolated Desktop profiles opened the first-run welcome and untouched Cookie settings dialog alongside the existing Claude. Both own Code roots returned valid vendor `claude auth status` responses with `loggedIn:false`. Evidence: `build/claude-proxy-stage-0.json` and its exact-PID screenshots.
- Final production launch with the saved proxy and all new-profile flags enabled: two independent processes, own Desktop data, exact private `CLAUDE_CONFIG_DIR` and credential-free loopback settings confirmed. The existing Claude stayed alive; both test apps quit normally and all owned helpers/scratch data were removed. No launch-environment postprocessing was used. Evidence and source hashes: `build/claude-final-0.14.0.json` and its screenshots.
- The saved upstream rejected proxy authentication with HTTP 407, established by the production bridge's categorical check against `claude.ai:443`. Consequently the local bridge returned CONNECT 502 and Claude displayed `ERR_TUNNEL_CONNECTION_FAILED`. Successful TLS routing, onboarding through this proxy and stable remote egress addresses were **not** established. Credentials were neither printed nor changed.
- A real compatibility test found that Claude 4.4212.0 refuses startup with `--host-resolver-rules`. AppDeck now passes this option only to the separate Chromium browser; the fixture asserts it is absent from Claude. No vendor startup guard is bypassed.
- Actual new-profile dialog screenshots in English and Russian reviewed, plus main-window preview renders with a new private-Code card. Controls and text fit. Synthetic proxy names were used in UI captures.
- Localization and portable suites passed: 829 English/Russian pairs; core 301, edge 485 417, workspace filter 68, usage 1 469, proxy 157, project sync 44, automation sync 80 and Claude session policy 216 assertions. Offline Claude session fixtures passed 99 assertions per architecture.

`APPDECK_GUI_SELFTEST=1 make verify` passed (exit 0; `build/final-verification-0.14.0.log`): universal warnings-as-errors build, system ad-hoc signature and static bundle checks, the suites above, 76 offline proxy checks, 142 ordinary-app assertions per architecture, native Codex project fixtures and 190 group-sync assertions, signed-out safe-mode Claude Code `get_usage`, API/AX smoke tests, and 17 side-panel checks for each architecture/language combination. The extracted release-candidate ZIP passed integrity, system `codesign --verify --deep --strict`, both architecture checks and byte comparison with the built bundle; both offline help pages are included (`build/package-validation-0.14.0.json`).

Open limits: no account sign-in or authenticated Code/Cowork turn was performed. Project and organization policies, macOS permissions and the system Keychain remain outside local profile isolation. Chrome routing is covered by the native stand-in, not an authenticated real Chrome session. No hosts/firewall/Tailscale/VPS configuration was changed, and no complete IP/DNS/UDP confinement is claimed. A functioning proxy and an OS/network enforcement boundary are required to validate that requirement; see [Claude network setup](CLAUDE_NETWORK.md). Intel hardware, macOS 13–15, Developer ID signing and notarization remain untested. The 0.13.0 evidence below is historical.

## Previous status of 0.13.0

0.13.0 combines ordinary application launch support, a clean/shared-session choice for new Claude Desktop profiles, and an opt-in Claude local Code session bridge. It also corrects the earlier claim that a shared `~/.claude` automatically shares Desktop sidebars. The native resume handler was inspected statically in installed Claude Desktop 2.31226.1 and is gated to that exact version.

### 0.13.0 clean-profile verification

The final clean Desktop implementation passed `APPDECK_GUI_SELFTEST=1 make verify` on this Mac (exit 0; `build/final-verification-0.13.0.log`), together with real-app and visual acceptance:

- Real Claude Desktop 4.4212.0: two disposable profiles launched through the exact production path, without environment postprocessing, alongside the user's already-running Claude. Screenshots of each exact test PID showed the untouched Claude for Mac welcome screen and initial Cookie settings dialog. Each had a different process and private user-data folder; the existing Claude remained running. No onboarding button was clicked, no account was signed in and no model turn was sent. Both test processes quit normally and their temporary data was removed. Evidence: `build/claude-final-onboarding.json` and its window screenshots.
- Native profile fixtures: 133 assertions on each of arm64 and x86_64/Rosetta with ASan/UBSan. The child retains its original scratch `HOME`, has no `CLAUDE_CONFIG_DIR` or `CFFIXED_USER_HOME`, persists its own Desktop data, and leaves the shared global-Code sentinel unchanged. Clean profiles stay outside the bridge; standard profiles retain their earlier behavior.
- Final dialog screenshots in English and Russian: both mode choices and all explanatory text fit. The Russian global-Code note wraps to two lines. Native popup controls are not drawn reliably by the separate `cacheDisplay` render path; actual window screenshots were used for this review.
- Localization: 793 English/Russian pairs. Portable ASan/UBSan suites: core 301, edge 485 417, workspace filter 68, usage 1 469, proxy 157, project sync 44, automation sync 80 and Claude session policy 216 assertions. Native Claude session scanner/reservation fixtures passed 99 assertions per architecture. The final universal build, system ad-hoc signature verification and static bundle checks passed.
- Final integration checks: 142 ordinary-app assertions per architecture, 76 offline proxy checks, Codex project protocol and native vendor-helper fixtures, 190 production group-sync assertions, signed-out Claude Code safe-mode `get_usage`, API smoke tests and the untrusted-bundle Accessibility regression. The ordinary-app fixture waits for raw NSWorkspace membership and `isFinishedLaunching` before its unchanged candidate assertions, removing a fixture startup race without changing production launch behavior.
- Side panel: 17 checks for each of arm64/English, arm64/Russian, x86_64/English and x86_64/Russian.
- The packaged ZIP was extracted and checked with system `codesign --verify --deep --strict`; its contents matched the final build. Signing is local ad hoc, without Developer ID signing or notarization.

In 0.13.0, Claude preserved real `HOME` so macOS Security could use the normal login Keychain; redirecting it blocked first-run startup. That release did not set `CFFIXED_USER_HOME` or `CLAUDE_CONFIG_DIR`. Global Code settings and transcripts remain in the normal user's `~/.claude`; the verified result is fresh Desktop onboarding, not a fresh macOS identity or isolated CLI configuration.

Static inspection confirmed that signed Claude Desktop 4.4212.0 ignores the legacy `CLAUDE_USER_DATA_DIR` override. Desktop storage uses `--user-data-dir`; additional profiles launch the unchanged executable through `NSTask` with an exact environment and the mode-specific working directory. The local-session import gate remains restricted to 2.31226.1. Real-account sign-in, separate account use after onboarding, and multi-account Code conversations remain unverified.

### Prior Claude bridge checks

Before integration with ordinary application launch support, bridge verification was deliberately offline: portable policy/localization suites, production scanner/reservation/event-construction fixtures for both architectures with ASan/UBSan, a universal warnings-as-errors build, system `codesign` and the static bundle checker. Those checks did not launch a managed application or the AppDeck GUI, modify a real profile, or replace the installed AppDeck bundle. That earlier verification did not run `make verify`, which launches vendor helpers and a GUI regression app. The panel implementation was unchanged.

Not verified for the new bridge: delivery to one selected real Claude instance, macOS Automation permission, sign-in and current-account changes, native Code sidebar registration and continued conversation under a second real account, imported trust/permission behavior, interface renders in both languages, and external CLI/Desktop writers. The Chrome extension's relocation restriction is not fixed by this feature. The handler may change after a Claude update; unknown versions disable import. Offline success is not a claim that these live checks passed.

### Prior ordinary-application checks

Unknown native `.app` bundles now register as ordinary applications with one launch entry and no profile data folders. Known adapters can also select ordinary launch. Proxy delivery is environment-only for ordinary apps, with a copyable loopback address for manual setup.

Before integration with the Claude changes, verified on this Mac:

- `make test`: 706 localized English/Russian pairs; core 301, edge 485 417, workspace filter 68, usage 1 469, proxy 157, project sync 44 and automation sync 80 assertions with ASan/UBSan.
- `make verify` passed after the AnyDesk launch fix: universal build and system ad-hoc signing checks, static bundle verification, 76 offline proxy-bridge checks, both native generic-app runners, the offline project protocol fixture, API/AX smoke tests and signed-out Claude Code `get_usage`. Native Codex project/group fixtures were skipped because no vendor helper was available at the verifier's default paths. The preceding UI revision also passed `APPDECK_GUI_SELFTEST=1 make verify`, including 17 side-panel checks for each architecture/language combination; the launch fix does not change the panel UI.
- Native generic-app fixtures: the expanded final regression suite passed 131 assertions each on arm64 and x86_64 under Rosetta. Real LaunchServices launches test registration, no per-app data folders, repeat launch and process adoption, state reload, explicit direct-launch environment overrides, delivery of a credential-free loopback proxy address, stale metadata after an external relaunch and bridge termination with its watched app. Existing profile creation remains available to known adapters. Fixture applications make no network requests and use only scratch data.
- The AnyDesk service-only regression failed before the fix: a same-bundle prohibited worker started directly through `NSTask` was selected as the app window. The fixed suite keeps that worker alive during ordinary and proxy launches, rejects persisted service identities, recovers directly started UI processes without a LaunchServices date through kernel birth timestamps, rejects changed timestamps, prefers regular UI for ordinary apps and preserves ambiguity for known account adapters. Read-only diagnosis of installed AnyDesk 9.8.1 confirmed a prohibited tray process with no LaunchServices launch date; no real AnyDesk service was stopped or modified.
- Main-window renders in English and Russian reviewed for the ordinary-app card and its manual-proxy instructions.
- Proxy acceptance now waits for its refusing HTTP fixture to consume 64 KiB before returning 407, explicitly proving the non-replayable streaming precondition. The earlier immediate refusal raced the upload and could legitimately allow safe failover. The deterministic rejection case passed 20 isolated repetitions; the existing 8 MiB integrity cases remain unchanged.

Compatibility limits: native applications may ignore proxy environment variables. The native fixture verifies configuration delivery and bridge lifetime, not the traffic route of AnyDesk or another vendor app. AnyDesk's own HTTP-proxy settings require manual host/port entry; the dynamic port changes on a new launch, and an installed background service can outlive the watched GUI process. No real AnyDesk session, account, provider proxy, Intel hardware or macOS 13–15 acceptance was performed for this change.

## Previous status of 0.12.1

0.12.1 changes only where AppDeck looks for Codex's helper. ChatGPT 26.924 (Codex CLI 0.158.0-alpha.2.1) moved it from `Contents/Resources/codex` to `Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex`, the path its desktop code starts on macOS. Verified on macOS 26.2, Apple Silicon, with that ChatGPT build:

- `make verify` passed with the same counts as 0.12.0, and the native project adapter and 190 group-sync assertions now run against the new helper's `app-server`; `group_sync_test` checks that the old layout is still found and the new one is preferred. Before the fix `verify.sh` also looked only at `Resources/codex` and skipped the native checks on this build, which is why the move went unnoticed.
- The side panel and the pages are unchanged; the GUI self-test was not rerun.

Verified for 0.12.0 (still valid for 0.12.1) on macOS 26.2, Apple Silicon, Apple clang 17:

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
- The `codex-cli` helper layout of an Intel build of ChatGPT (only the arm64 build was available).

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

- Adding `Claude.app` preselects the Claude Desktop adapter. **New profile** and **Duplicate** default to clean Desktop + Code mode and offer shared-session mode. Confirm existing profiles keep their mode after reload and cards display the saved mode in both languages.
- Launch two disposable clean profiles while the original is running: each must have its own first-run window, user-data, working folder and temporary path, while preserving the real macOS home. Restart and manager re-adoption must return to the same profile. Confirm that both modes retain global `~/.claude` settings and transcripts. Verify sign-in and Code conversations with separate test accounts; an argument marker alone does not prove complete account isolation.
- With inherited profile/API overrides, verify that a newly selected clean Claude child receives only its own private `CLAUDE_CONFIG_DIR`; older Desktop-only clean, standard and primary profiles receive no AppDeck-selected override. Foreign credential variables are removed. Check the expected private or usual working directory, exact-PID registration and process adoption when `launchDate` is absent.
- Check saved/default/direct proxy choices, missing required proxy rejection, five traffic switches, safe private settings reconciliation, and linked Chrome creation/reuse. Confirm Claude never receives the rejected host-resolver switch. Test failed browser setup followed by Cancel, and a deleted browser link: existing Claude settings must remain intact and a dangling link must not force browser recreation.
- Keep a shared-session reservation active, then launch a clean profile: it must remain outside the reservation, catalogue and target list. Another shared-mode profile must remain blocked. Unknown or malformed mode values must not silently migrate an existing profile.
- Inspect compatibility with installed Claude 4.4212.0. It and the previously inspected 2.31226.1 signed build ignore the legacy `CLAUDE_USER_DATA_DIR`; the earlier build disabled Chrome-extension local pairing for relocated data. macOS permissions and Keychain identity remain OS-scoped.
- The primary card attaches to the running original and never adopts a copy.
- Local Code bridge: opt in, discover source conversations, open one in a second signed-in profile with every other Desktop and CLI writer closed, confirm the exact target sidebar and continued context/account. A denied pid-addressed Apple event must not open an arbitrary instance or report success.
- Check retry, native account switch, missing transcript, conflicting cwd, archive-only records, corrupt or missing reservation, manager restart and native-process exit/restart. During ownership, AppDeck must refuse a different shared-mode profile launch while allowing clean profiles. Native metadata must be written by Claude only; cookies, permission settings, transcripts and schedules are never copied.
- Review Code sessions and Shared settings at the minimum window size in both languages before enabling this feature in a live workflow.

## Release gate

A production release needs the GUI, permission and multi-account checks above on Apple Silicon and Intel, on the oldest supported and the current macOS, Apple `codesign` verification, Developer ID signing and notarization, and a compatibility record per target app version. Passing unit tests alone does not meet this gate.
