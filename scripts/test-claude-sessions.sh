#!/bin/bash
# Offline Foundation/Apple-event construction fixtures; never runs NSApplication or Claude.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for arch in arm64 x86_64; do
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$arch" -fsanitize=address,undefined \
    tests/claude_sessions_test.cpp -framework AppKit -lobjc -o "build/tests/claude_sessions-$arch"
  "build/tests/claude_sessions-$arch"
done
