#!/bin/bash
# Install build/AppDeck.app into /Applications (or the folder given as the first argument).
# AppDeck must not be running: quit it from its menu first (your profile windows keep running).
set -euo pipefail
cd "$(dirname "$0")/.."
SOURCE="build/AppDeck.app"
TARGET="${1:-/Applications}/AppDeck.app"
[[ -d "$SOURCE" ]] || { echo "Nothing to install: run make first."; exit 1; }
if pgrep -x AppDeck >/dev/null; then
  echo "AppDeck is running. Quit it (AppDeck → Quit AppDeck) and run this again."; exit 1
fi
rm -rf "$TARGET"
/usr/bin/ditto "$SOURCE" "$TARGET"
/usr/bin/codesign --verify --deep --strict "$TARGET"
echo "Installed $TARGET"
