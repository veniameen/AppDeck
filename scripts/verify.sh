#!/bin/bash
# Full acceptance on a Mac: build, portable tests, static bundle check, native sync fixtures,
# macOS API/AX smoke tests and (with APPDECK_GUI_SELFTEST=1) the side panel self-test.
# Every check uses fresh scratch folders: no real profile, sign-in or model turn is involved.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "$(uname -s)" == Darwin ]] || { echo "Run this on macOS."; exit 1; }
APP="$PWD/build/AppDeck.app"
OUT="$PWD/build/verify"
rm -rf "$OUT"; mkdir -p "$OUT"
STRICT=(xcrun clang++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable -mmacosx-version-min=13.0)

scripts/build.sh
scripts/test.sh

# The binary imports only system libraries and carries a complete ad-hoc signature (both slices).
python3 tools/verify_bundle.py "$APP"

# Production project adapter and group synchronizer against isolated profiles. These tests never
# sign in, send a model turn or open a real profile database.
for t in project_server_sync_test group_sync_test; do
  "${STRICT[@]}" -O1 "tests/$t.cpp" -framework AppKit -lobjc -o "$OUT/$t"
done
SUFFIX="$(uuidgen)"
"$OUT/project_server_sync_test" "$PWD/tests/project_server_fixture.py" "/private/tmp/appdeck-project-adapter-protocol-$SUFFIX" --protocol-fixture
rm -rf "/private/tmp/appdeck-project-adapter-protocol-$SUFFIX"
CODEX_BINARY="${APPDECK_CODEX_BINARY:-/Applications/ChatGPT.app/Contents/Resources/codex}"
if [[ -x "$CODEX_BINARY" ]]; then
  "$OUT/project_server_sync_test" "$CODEX_BINARY" "/private/tmp/appdeck-project-adapter-native-$SUFFIX"
  rm -rf "/private/tmp/appdeck-project-adapter-native-$SUFFIX"
  "$OUT/group_sync_test" "$CODEX_BINARY"
else
  echo "SKIP: the bundled Codex is not installed; native project/group checks were not run."
fi

# Real-API smoke test: selectors and constants, struct ABI, the no-prompt Accessibility query and
# recovery of --user-data-dir through sysctl(KERN_PROCARGS2).
"${STRICT[@]}" tests/macos_api_smoke.cpp -framework AppKit -lobjc -o "$OUT/api-smoke"
"$OUT/api-smoke"
"$OUT/api-smoke" --user-data-dir=/tmp/appdeck-smoke/user-data
# A real exchange with Claude Code when it is installed: signed-out scratch config, --safe-mode, no model turn.
CLAUDE_BIN="$HOME/.local/bin/claude"
[[ -x "$CLAUDE_BIN" ]] || CLAUDE_BIN="$(ls -d "$HOME/Library/Application Support/Claude/claude-code"/*/claude.app/Contents/MacOS/claude 2>/dev/null | sort -V | tail -1)"
if [[ -n "$CLAUDE_BIN" && -x "$CLAUDE_BIN" ]]; then
  mkdir -p "$OUT/claude/config" "$OUT/claude/cwd"
  env -u CLAUDE_CONFIG_DIR -u ANTHROPIC_API_KEY "$OUT/api-smoke" --claude-probe "$CLAUDE_BIN" "$OUT/claude/config" "$OUT/claude/cwd"
else
  echo "SKIP: Claude Code is not installed; the live get_usage exchange was not run."
fi

# Regression for the 0.2.0 crash. A terminal is usually trusted for Accessibility, which hides the
# bug, so the same query also runs as a standalone bundle with its own (untrusted) identity — the
# situation of every fresh install.
SMOKE="$OUT/AXSmoke.app"; LOG="$OUT/ax-smoke.log"
mkdir -p "$SMOKE/Contents/MacOS"
cp "$OUT/api-smoke" "$SMOKE/Contents/MacOS/AXSmoke"
cat > "$SMOKE/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>AXSmoke</string>
<key>CFBundleIdentifier</key><string>local.appdeck.axsmoke</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSUIElement</key><true/>
</dict></plist>
PLIST
/usr/bin/codesign --force --sign - "$SMOKE" >/dev/null 2>&1
/usr/bin/open -W -n "$SMOKE" --args --ax-log "$LOG" >/dev/null 2>&1 || true
for _ in 1 2 3 4 5 6; do [[ -s "$LOG" ]] && break; sleep 0.5; done
if grep -q "ax-query-survived" "$LOG" 2>/dev/null; then
  echo "PASS: no-prompt Accessibility query survives in an untrusted bundle ($(cat "$LOG"))."
else
  echo "FAIL: the untrusted-bundle Accessibility query did not finish (see ~/Library/Logs/DiagnosticReports/AXSmoke-*)."; exit 1
fi

# Optional GUI acceptance of the side panel: shows a sandbox panel for about 20 s per architecture
# and language and replays clicks and drags through AppKit's own event queue (the pointer does not
# move, no profile is launched). Sandbox data only.
if [[ "${APPDECK_GUI_SELFTEST:-}" == 1 ]]; then
  for ARCH in arm64 x86_64; do
    for LANGUAGE in en ru; do
      ROOT="/private/tmp/appdeck-panel-selftest-$(uuidgen)"
      python3 tools/make_dock_sandbox.py "$ROOT" 9 >/dev/null
      LOGFILE="$OUT/panel-selftest-$ARCH-$LANGUAGE.log"
      if ! APPDECK_DATA_ROOT="$ROOT" APPDECK_NO_TERMINAL=1 APPDECK_DOCK_SELFTEST=1 \
           arch -"$ARCH" "$APP/Contents/MacOS/AppDeck" -AppleLanguages "($LANGUAGE)" > "$LOGFILE" 2>&1; then
        cat "$LOGFILE"; rm -rf "$ROOT"; echo "FAIL: side panel self-test ($ARCH, $LANGUAGE)."; exit 1
      fi
      rm -rf "$ROOT"
      echo "PASS: side panel self-test ($ARCH, $LANGUAGE): $(grep -c '^PASS' "$LOGFILE") checks."
    done
  done
else
  echo "SKIP: side panel GUI self-test (APPDECK_GUI_SELFTEST=1 runs it)."
fi

printf '\nAll checks passed: %s\n' "$APP"
