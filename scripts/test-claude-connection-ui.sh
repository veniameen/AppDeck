#!/bin/bash
# In-memory form interactions and offscreen EN/RU renders; no managed apps or real data.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for test_arch in arm64 x86_64; do
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$test_arch" -fsanitize=address,undefined \
    tests/claude_connection_ui_test.cpp -framework AppKit -lobjc -o "build/tests/claude-connection-ui-$test_arch"
  arch -"$test_arch" "build/tests/claude-connection-ui-$test_arch" "$PWD/build/connection-ui-qa/$test_arch"
done
