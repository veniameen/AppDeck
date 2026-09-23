# AppDeck development constraints

These rules apply to every change, by people and by coding agents. Background: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/SYNC_DESIGN.md](docs/SYNC_DESIGN.md), [docs/TESTING.md](docs/TESTING.md).

## Platform

Native C++17/AppKit through the public ABI bridge (`src/mac.hpp`). Maintain macOS 13+ and arm64/x86_64. Do not add private WindowServer APIs, blanket event taps, silent permission bypasses, Electron, or modifications of managed app bundles. The binary imports only system libraries (`tools/verify_bundle.py` checks this). Use `mac::alignCenter`/`alignRight`, never literal NSTextAlignment numbers (they differ between arm64 and x86_64).

## Data safety

Preserve the source Codex profile. No full CODEX_HOME/user-data/DB/auth cloning. Always use explicit allowlists and independent account storage. Shared history requires explicit per-group consent, Codex's own CODEX_SQLITE_HOME switch, and the three `deck::historyLeaf` folder links that never replace existing local data. With sharing enabled, the versioned AppDeck group journal also reconciles allowlisted local projects and supported automation definitions across the base and linked copies. Never copy, symlink or edit a Codex database, and never share auth.json, cookies or Electron user data.

Project synchronization uses the vendor app-server project and thread-metadata APIs plus allowlisted Desktop cache fields; direct database access remains prohibited. The base and linked copies may receive project-cache changes only while STOPPED, after durable group-journal reconciliation, with the previous file kept and an unchanged-input recheck. Preserve unrelated Desktop data and account-specific/cloud projects. Persist per-profile baselines and explicit deletion tombstones; unknown/malformed input never means an empty registry. Automation synchronization copies only validated automation.toml definitions, leaves all other automation files intact, and gives each schedule exactly one owner; nonowner copies remain PAUSED. Change owners only with all participating desktop instances stopped, pause old copies before activating the new owner, and refuse activation after a failed pause. Never silently activate a globally paused schedule. Unknown schema is a compatibility limitation, not a reason to copy arbitrary data. Preserve organization restrictions in config.

## Account limits

Codex limits may only come from the bundle's own `codex app-server` (account/read, account/rateLimits/read with refreshToken:false): never open, stat, link or parse auth.json, never call OpenAI endpoints from AppDeck, never run probes in parallel or faster than `usage_policy.hpp` allows, and never give the probe an empty CODEX_SQLITE_HOME (Codex re-indexes every rollout and times out). Claude limits may only come from Claude Code itself (`claude auth status`, then one `get_usage` control request in `--safe-mode` with `--no-session-persistence`) run against the profile's own meter folder under AppDeck's data root: never point a probe at `~/.claude`, never set CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC for it (Claude Code then skips the usage request and returns rate_limits:null), parse the server's `limits[]` list before the derived fields, never send a user message, never read the Keychain or Claude Desktop's cookies, never perform the sign-in for the user. Cache display data only; keep account e-mails out of diagnostics.

## Windows and the side panel

Never call AXIsProcessTrustedWithOptions with a dictionary that lacks kAXTrustedCheckOptionPrompt (it crashes untrusted processes; see `ax_api.hpp`) and keep the untrusted-bundle regression in `scripts/verify.sh`. Bring a window forward with the per-pid reopen Apple event plus activation (`window_control.hpp`), never by starting the bundle again through LaunchServices: with several instances of one bundle that would hit an arbitrary account. Claude Desktop copies get CLAUDE_USER_DATA_DIR and never a CLAUDE_CONFIG_DIR of AppDeck's choosing. Do not claim an app window is permanently topmost just because AXRaise succeeded.

The `profiles` array in state.plist is the user's order: the side panel, Control-Option-1…8, the menu bar menu and (per app) the main window follow it. Never re-sort it; change it only through `moveProfileNextTo`/`moveProfileInList`. The one-time 0.9 migration (`profileOrder` flag) adopted the earlier app-grouped order. The panel shows at most six rows and scrolls; at rest it has no shade (`deck::edgeCut`): keep the default look clean. `APPDECK_DOCK_SELFTEST` works only together with `APPDECK_DATA_ROOT`; run it for both architectures and both languages after touching the panel (`APPDECK_GUI_SELFTEST=1 make verify`).

## Proxies

Proxy credentials live only in `proxies.plist` (0600) and in memory; never put them in `state.plist`, argv, the environment, logs or diagnostics (counts only). The bridge `appdeck-proxy` binds 127.0.0.1 only, gets its configuration on stdin, never speaks TLS itself, and exits with the process it watches. The endpoint and protocol rules stay in `proxy_policy.hpp` with tests in `tests/proxy_test.cpp`; do not weaken the loopback bypass, `--disable-quic` or the WebRTC policy (they keep traffic inside the proxy).

## Localization

The interface is English and Russian; documentation, comments and commit messages are English. Write every user-visible string as `T("English", "Русский")` at its point of use; keep printf conversions identical in both halves. `make test` runs `tools/check_i18n.py`, which rejects Cyrillic outside a `T()` pair. Check new screens in both languages (`-AppleLanguages '(ru)'`).

## Verification

`make test` runs the localization check and the portable suites (core, edge, workspace filter, usage, project sync, automation sync) with ASan/UBSan. `make verify` builds both architectures with warnings as errors and adds the static bundle check, the native project/group sync fixtures, the API smoke test and the untrusted-bundle regression. Use `APPDECK_DATA_ROOT` so acceptance runs never touch real profiles, and `APPDECK_RENDER_DIR` to review every screen. Do not claim that compilation or static signature checks are real Mac execution or Apple codesign validation. Every release updates the verification status and its open limitations in `docs/TESTING.md`. Prefer the system `codesign` for local signing; production needs Developer ID signing and notarization.
