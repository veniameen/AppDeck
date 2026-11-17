#!/bin/bash
# Build build/AppDeck.app: universal (arm64 + x86_64), macOS 13+, warnings as errors, ad-hoc signed.
# Needs the Apple Command Line Tools, system python3 and the Go toolchain for the VLESS helper.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "$(uname -s)" == Darwin ]] || { echo "AppDeck builds on macOS."; exit 1; }
xcrun --find clang++ >/dev/null 2>&1 || { echo "Install the Apple Command Line Tools: xcode-select --install"; exit 1; }

APP="build/AppDeck.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# One translation unit, system frameworks only: no third-party code, no Xcode project.
xcrun clang++ -std=c++17 -O2 -fno-exceptions -fno-rtti -fstack-protector-strong \
  -Wall -Wextra -Werror -Wno-unused-variable -mmacosx-version-min=13.0 \
  -arch arm64 -arch x86_64 src/main.cpp -framework AppKit -lobjc \
  -o "$APP/Contents/MacOS/AppDeck"

# The local proxy bridge: a separate executable that links libSystem only (no AppKit, no C++ runtime).
# It is signed on its own first; the bundle signature then seals it as nested code.
xcrun clang++ -std=c++17 -O2 -fno-exceptions -fno-rtti -fstack-protector-strong -nostdlib++ \
  -Wall -Wextra -Werror -mmacosx-version-min=13.0 -arch arm64 -arch x86_64 \
  src/proxy_bridge.cpp -o "$APP/Contents/MacOS/appdeck-proxy"

# VLESS is a separate GPL helper, with no third-party code linked into the AppKit executable.
# Include its complete corresponding source and license notices with every distributed bundle.
bash scripts/build-vless.sh
cp build/appdeck-vless "$APP/Contents/MacOS/appdeck-vless"
mkdir -p "$APP/Contents/Resources/vless"
cp tools/vless-helper/LICENSE tools/vless-helper/COPYING tools/vless-helper/THIRD_PARTY_NOTICES.md "$APP/Contents/Resources/vless/"
cp build/appdeck-vless-source.tar.gz "$APP/Contents/Resources/vless/source.tar.gz"

cp assets/Info.plist "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
cp assets/AppDeck.icns "$APP/Contents/Resources/"
# The user guide (English) and the two interface localizations: en.lproj and ru.lproj tell macOS
# which languages AppDeck speaks, so AppKit and AppDeck pick the same one.
python3 tools/make_help.py "$APP/Contents/Resources" >/dev/null
cp -R assets/en.lproj assets/ru.lproj "$APP/Contents/Resources/"

/usr/bin/codesign --force --sign - --identifier local.appdeck.proxy "$APP/Contents/MacOS/appdeck-proxy"
/usr/bin/codesign --force --sign - --identifier local.appdeck.vless "$APP/Contents/MacOS/appdeck-vless"
/usr/bin/codesign --force --sign - "$APP"
/usr/bin/codesign --verify --deep --strict "$APP"
echo "Built $APP ($(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist"), $(lipo -archs "$APP/Contents/MacOS/AppDeck"))"
