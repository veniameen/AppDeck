#!/bin/bash
# Real production bridge and launch wiring, with disposable native stand-ins only.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for test_arch in arm64 x86_64; do
  test_app="$PWD/build/tests/ClaudeNetworkTest-$test_arch.app"
  mkdir -p "$test_app/Contents/MacOS"
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$test_arch" tests/claude_profile_fixture.cpp \
    -framework AppKit -lobjc -o "build/tests/claude-network-fixture-$test_arch"
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$test_arch" -fsanitize=address,undefined \
    tests/claude_network_test.cpp -framework AppKit -lobjc -o "$test_app/Contents/MacOS/ClaudeNetworkTest"
  xcrun clang++ -std=c++17 -O2 -fno-exceptions -fno-rtti -fstack-protector-strong -nostdlib++ \
    -Wall -Wextra -Werror -mmacosx-version-min=13.0 -arch "$test_arch" \
    src/proxy_bridge.cpp -o "$test_app/Contents/MacOS/appdeck-proxy"
  cat > "$test_app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>ClaudeNetworkTest</string>
<key>CFBundleIdentifier</key><string>local.appdeck.claude-network-test</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSUIElement</key><true/>
</dict></plist>
PLIST
  /usr/bin/codesign --force --sign - --identifier local.appdeck.network-fixture-proxy "$test_app/Contents/MacOS/appdeck-proxy" >/dev/null 2>&1
  /usr/bin/codesign --force --sign - "$test_app" >/dev/null 2>&1
  arch -"$test_arch" "$test_app/Contents/MacOS/ClaudeNetworkTest" "$PWD/build/tests/claude-network-fixture-$test_arch"
done
