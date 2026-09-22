# References

Checked 18 September 2026. Desktop implementation details are deliberately treated differently from public API contracts.

## Apple public APIs

- NSPanel: https://developer.apple.com/documentation/appkit/nspanel
- NSVisualEffectView: https://developer.apple.com/documentation/appkit/nsvisualeffectview
- AXUIElement: https://developer.apple.com/documentation/applicationservices/axuielement
- NSWorkspace.OpenConfiguration: https://developer.apple.com/documentation/appkit/nsworkspace/openconfiguration
- Registered Carbon hotkeys are used via the system Carbon framework; no event tap, screen recording or private WindowServer functions are used.

These are API references. `tests/macos_api_smoke.cpp` verifies the required runtime selectors and symbols on a real Mac (`make verify`).

## OpenAI authentication/configuration

- https://developers.openai.com/codex/auth/ (redirects to https://learn.chatgpt.com/docs/auth)
- https://developers.openai.com/codex/config-basic/ (redirects to https://learn.chatgpt.com/docs/config-file/config-basic)

The file credential store and separate CODEX_HOME are documented. GUI environment-variable behavior and desktop project registries are not promised as stable contracts here.

## Desktop implementation observations — issue reports, not guaranteed APIs

- https://github.com/openai/codex/issues/44777 — an isolated Desktop startup using CODEX_HOME, CODEX_ELECTRON_USER_DATA_PATH and --user-data-dir.
- https://github.com/openai/codex/issues/22075 — observed root/label/order fields in `.codex-global-state.json`.
- https://github.com/openai/codex/issues/25385 — project registration/internal source-of-truth limitations; manual state changes may be undone or ignored by Desktop.

Those reports motivate a conservative, explicitly experimental projection into NEW profiles only. They do not establish that modern Codex supports full project sync or that arbitrary Desktop state is safe to share.
