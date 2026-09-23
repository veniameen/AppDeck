<div align="center">

<img src="assets/AppDeck.png" width="112" alt="AppDeck icon">

# AppDeck

**Several accounts of the same Mac app, side by side — one window per account, one shared workspace.**

[![CI](https://github.com/veniameen/AppDeck/actions/workflows/ci.yml/badge.svg)](https://github.com/veniameen/AppDeck/actions/workflows/ci.yml)
![macOS 13+](https://img.shields.io/badge/macOS-13%2B-111111?logo=apple)
![Universal](https://img.shields.io/badge/Apple%20Silicon%20%2B%20Intel-universal-555555)
![C++17 · AppKit](https://img.shields.io/badge/C%2B%2B17-AppKit-00599C?logo=cplusplus)
[![License: MIT](https://img.shields.io/badge/license-MIT-2f6fde)](LICENSE)

<img src="docs/images/hero.png" alt="The AppDeck main window with four Codex profiles and their usage limits, and the side panel listing six profiles of Codex and Claude" width="100%">

</div>

AppDeck is a small native macOS app for people who work in the same desktop app under several accounts — typically [Codex](https://openai.com/codex/) (`ChatGPT.app`) or [Claude Desktop](https://claude.ai/download) with a personal and a work subscription. Every profile is an ordinary window of the original app with its own sign-in. AppDeck launches them, keeps their data apart, shows how much of each account's usage limit is left and lets you switch between them from anywhere.

## Features

- **Separate sign-ins, shared work.** Each profile gets its own private data folder, so every window stays signed in to its own account. For Codex you can share settings, chat history, projects and automations between the accounts — without copying a single database or token.
- **Usage limits at a glance.** The weekly, 5-hour and per-model windows of every Codex and Claude account with their reset times, asked from the apps' own tools. The account with the most headroom stands out.
- **Side panel.** Press <kbd>⌃</kbd><kbd>⌥</kbd><kbd>Space</kbd> anywhere: a slim glass panel unfolds at the right edge of the screen. Click a row or press <kbd>⌃</kbd><kbd>⌥</kbd><kbd>1</kbd>…<kbd>8</kbd> to launch or switch, drag rows into your own order, scroll when you have more than six profiles.
- **Window tools.** Arrange four windows in a 2×2 grid and put them back, hide every profile at once, reopen a window that was closed with the red button.
- **Native and light.** One C++17 translation unit on public AppKit and Objective-C runtime APIs. No Electron, no WebView, no network client, no telemetry. A universal binary for macOS 13 and later.
- **Graphite glass.** A dark translucent interface in the manner of macOS 26: one bone-white ink, capsule controls of one height, colour only for state and for each profile's badge.
- **English and Russian.** The interface follows your macOS language.

## Supported apps

| App | What a profile gets | Extras |
|---|---|---|
| Codex (`ChatGPT.app`) | Its own `CODEX_HOME`, Electron user data and sign-in | Shared settings, history, projects and automations (opt-in); usage limits |
| Claude Desktop | Its own `CLAUDE_USER_DATA_DIR` and sign-in | Usage limits through a Claude Code sign-in |
| VS Code, Cursor | Its own user data and extensions folder | — |
| Chrome, Brave, Edge, Chromium | Its own browser profile folder | — |
| Other Electron apps | Its own user data folder (experimental) | — |

The original app is never copied or modified: AppDeck starts it with its own documented switches.

## Install

**Build from source** — needs macOS 13+ and the Apple Command Line Tools (`xcode-select --install`), nothing else:

```sh
git clone https://github.com/veniameen/AppDeck.git
cd AppDeck
make install          # builds build/AppDeck.app and copies it to /Applications
```

**Or download** `AppDeck.zip` from [Releases](https://github.com/veniameen/AppDeck/releases), unzip it and move `AppDeck.app` to Applications with Finder before the first launch.

AppDeck is signed ad hoc, not with a Developer ID, and is not notarized. On first launch macOS may refuse to open it: open **System Settings → Privacy & Security** and click **Open Anyway** for AppDeck. Please do not disable Gatekeeper.

## Quick start

1. **＋ Add app** in the sidebar and choose `ChatGPT.app`, `Claude.app` or another supported app.
2. Create a profile per account with **New profile**. For Codex, **Shared settings → Connect Codex…** keeps your current Codex as the primary profile and lets the others share its workspace.
3. **Launch** a profile and sign in inside its window. Sign in to one new account at a time.
4. Turn on **Account limits…** to see what every account has left, and press <kbd>⌃</kbd><kbd>⌥</kbd><kbd>Space</kbd> to switch from anywhere.

The [user guide](docs/USER_GUIDE.md) explains every option; it is also available inside the app under **About → User guide**.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| <kbd>⌃</kbd><kbd>⌥</kbd><kbd>Space</kbd> | Show or hide the side panel |
| <kbd>⌃</kbd><kbd>⌥</kbd><kbd>1</kbd> … <kbd>8</kbd> | Launch the profile in that place, or bring its window forward |
| <kbd>⌘</kbd><kbd>1</kbd> … <kbd>9</kbd> | The same for the selected app, while the AppDeck window is active |

The shortcuts are registered hot keys: no Input Monitoring permission is needed. **2×2** and **Restore** need the Accessibility permission; everything else works without it.

## Privacy and safety

- AppDeck never copies, links or edits the managed apps' databases and never reads or shares `auth.json`, cookies or Keychain items. Sign-ins stay in each profile's private folder.
- It has no network client. Codex limits come from the app's own `codex app-server` (`account/read`, `account/rateLimits/read`); Claude limits from Claude Code's `get_usage` request in `--safe-mode`, which sends no message to the model. AppDeck never sees a token.
- Everything it keeps is in `~/Library/Application Support/AppDeck`. Diagnostics contain no tokens, configuration contents or account addresses.
- Profiles are separated by data, not sandboxed: every app still runs as your macOS user. See [SECURITY.md](SECURITY.md).

## Limitations

- Not notarized; local builds are signed ad hoc, so macOS may ask for Accessibility again after an update.
- Codex's `app-server` protocol and desktop project cache are internal and may change with a new `ChatGPT.app`; AppDeck then shows “no data” or skips the sync instead of guessing.
- Some scenarios are not verified yet — for example several different real accounts sharing history at the same time, multi-monitor setups and Intel hardware. The current status is in [docs/TESTING.md](docs/TESTING.md).

## Development

```sh
make            # build/AppDeck.app (universal, warnings as errors, ad-hoc signed)
make test       # localization check + portable policy tests with ASan/UBSan
make verify     # full acceptance on this Mac: native sync fixtures, API and Accessibility smoke tests
```

```
src/        C++17 sources: main.cpp and its headers; pure policy headers are unit-tested
tests/      portable tests, macOS smoke and integration tests
tools/      help page, localization check, bundle verifier, sandbox and demo data
scripts/    build, test, verify and install
assets/     Info.plist, icon, localized Info.plist strings
docs/       user guide, architecture, sync design, testing
```

Read [CONTRIBUTING.md](CONTRIBUTING.md) and the hard rules in [AGENTS.md](AGENTS.md) before sending changes. Design notes: [architecture](docs/ARCHITECTURE.md), [project and automation sync](docs/SYNC_DESIGN.md), [references](docs/REFERENCES.md). Release notes: [CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE). AppDeck is an independent project, not affiliated with OpenAI or Anthropic; ChatGPT, Codex and Claude are trademarks of their owners.
