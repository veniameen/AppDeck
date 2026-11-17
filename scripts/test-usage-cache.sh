#!/bin/bash
# Native production cache + scheduler + CLI protocol, with synthetic account data only.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for cache_arch in arm64 x86_64; do
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$cache_arch" -fsanitize=address,undefined \
    tests/usage_cache_test.cpp -framework AppKit -lobjc -o "build/tests/usage-cache-$cache_arch"
  arch -"$cache_arch" "build/tests/usage-cache-$cache_arch" "$PWD/tests/usage_cache_vendor_fixture.py"
done
