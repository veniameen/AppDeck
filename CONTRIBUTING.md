# Contributing

Thanks for your interest in AppDeck! Bug reports, compatibility notes for new app versions and pull requests are welcome.

## Setup

You need macOS 13 or later and the Apple Command Line Tools (`xcode-select --install`). There is no Xcode project and no third-party dependency.

```sh
make            # build/AppDeck.app — universal, ad-hoc signed
make test       # localization check + portable policy tests (ASan/UBSan)
make verify     # full acceptance on this Mac (see docs/TESTING.md)
make install    # copy the build to /Applications (quit AppDeck first)
```

Run experiments against a separate data folder so your real profiles are never touched:

```sh
python3 tools/make_demo_data.py /tmp/appdeck-demo
APPDECK_DATA_ROOT=/tmp/appdeck-demo build/AppDeck.app/Contents/MacOS/AppDeck
```

## Guidelines

- Read [AGENTS.md](AGENTS.md) first. It lists the hard rules: public APIs only, no copying or editing of other apps' databases and credentials, strict limits on how account limits are read.
- Keep policy logic in the pure headers (`core.hpp`, `edge_policy.hpp`, `usage_policy.hpp`, `project_sync.hpp`, …) and cover it in `tests/`. UI code in `main.cpp` and the other headers stays thin.
- Every user-visible string is `T("English", "Русский")`. If you cannot write the Russian half, put the English text in both halves and say so in the pull request.
- Match the surrounding style: compact C++17, English comments that explain *why*.
- Describe in the pull request what you verified on a real Mac and what you could not.

## Reporting problems

Please include your macOS version, Mac model (Apple Silicon or Intel), the version of the managed app (for example `ChatGPT.app` or `Claude.app`) and the report from **About → Diagnostics…**. It contains no tokens, configuration contents or account addresses. For security issues see [SECURITY.md](SECURITY.md).
