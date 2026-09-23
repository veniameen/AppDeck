# Security

## Model

AppDeck separates the **data** of several accounts of one app; it is not a security sandbox. Every profile runs as your macOS user and can read the same files as any other app you run.

What AppDeck guarantees by design:

- It never copies, links or edits another app's databases, and never reads, copies or shares `auth.json`, cookies, Keychain items or Electron user data.
- It never modifies the bundles of the apps it manages and uses public macOS APIs only — no private WindowServer calls, no event taps, no screen recording.
- It has no network client of its own and no telemetry. Account limits are asked from the apps' own tools (`codex app-server`, Claude Code in `--safe-mode`) with the profile's own data; AppDeck never sees tokens.
- Proxies are opt-in. For a profile that uses one, the local bridge `appdeck-proxy` relays that app's traffic to the proxy you configured; it listens on `127.0.0.1` only and exits with the window. Proxy passwords are stored in `proxies.plist` (mode 0600) in AppDeck's data folder — like the apps' own `auth.json`, not in the Keychain — and are passed to the bridge through a pipe. Any program running as your user on this Mac can use a bridge while it runs.
- Profile folders are private (mode 0700), generated settings files 0600; symbolic links are refused where AppDeck writes.
- Diagnostics exclude tokens, configuration contents and account addresses.

Codex configuration can contain MCP keys and organization restrictions; when you choose to share settings, they are inherited by linked copies on purpose.

## Reporting a vulnerability

Please report security issues privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**. Do not open a public issue for them. Include the AppDeck version, macOS version and the steps to reproduce; never include tokens, `auth.json` or chat databases.
