#!/bin/bash
# Native synthetic form interactions and offscreen EN/RU renders; no profiles or network.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for test_arch in arm64 x86_64; do
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$test_arch" -fsanitize=address,undefined \
    tests/vless_ui_test.cpp -framework AppKit -lobjc -o "build/tests/vless-ui-$test_arch"
  arch -"$test_arch" "build/tests/vless-ui-$test_arch" "$PWD/build/vless-ui-qa/$test_arch"
done
