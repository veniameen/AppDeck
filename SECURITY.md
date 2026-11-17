# Security

## Model

AppDeck separates the **data** of several accounts of one app; it is not a security sandbox. Every profile runs as your macOS user and can read the same files as any other app you run.

What AppDeck guarantees by design:

- It never copies, links or edits another app's databases, and never reads, copies or shares `auth.json`, cookies, Keychain items or Electron user data.
- It never modifies the bundles of the apps it manages and uses public macOS APIs only — no private WindowServer calls, no event taps, no screen recording.
- The manager makes no account API requests itself and has no telemetry. Account limits are asked from the apps' own tools (`codex app-server`, Claude Code in `--safe-mode`) with the profile's own data; AppDeck never reads account tokens. Network helpers relay selected profiles' traffic and explicit connection checks.
- Proxies are opt-in. A selected profile gets a loopback bridge: `appdeck-proxy` for HTTP/SOCKS5 or `appdeck-vless` for VLESS TCP with TLS/REALITY. Each listens on `127.0.0.1` only and exits with its watched process. Proxy passwords and VLESS links are stored only in `proxies.plist` (mode 0600) and sent to helpers through stdin, never arguments or environment. Any program running as your user on this Mac can use a bridge while it runs.
- VLESS changes application proxy settings, not system routes, DNS or VPN configuration. It has no direct fallback inside the bridge and does not accept insecure certificate options, but applications and helpers that ignore proxy settings remain outside that route. This is not an OS firewall or a guarantee of complete IP/DNS isolation.
- Profile folders are private (mode 0700), generated settings files 0600; symbolic links are refused where AppDeck writes.
- Diagnostics exclude tokens, configuration contents and account addresses.

Codex configuration can contain MCP keys and organization restrictions; when you choose to share settings, they are inherited by linked copies on purpose.

## Reporting a vulnerability

Please report security issues privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**. Do not open a public issue for them. Include the AppDeck version, macOS version and the steps to reproduce; never include tokens, `auth.json` or chat databases.
