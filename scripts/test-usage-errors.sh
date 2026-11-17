#!/bin/bash
# Production error parsing against a fake CLI in the test binary; no vendor/network access.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for test_arch in arm64 x86_64; do
  test_binary="$PWD/build/tests/usage-error-fixture-$test_arch"
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
    -mmacosx-version-min=13.0 -arch "$test_arch" -fsanitize=address,undefined \
    tests/usage_error_fixture.cpp -framework AppKit -lobjc -o "$test_binary"
  arch -"$test_arch" "$test_binary" "$test_binary"
done
