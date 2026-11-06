#!/bin/bash
# Production launch adapter with disposable native fixture apps; never starts Claude.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
for test_arch in arm64 x86_64; do
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$test_arch" tests/claude_profile_fixture.cpp \
    -framework AppKit -lobjc -o "build/tests/claude-profile-fixture-$test_arch"
  xcrun clang++ -std=c++17 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-variable \
    -mmacosx-version-min=13.0 -arch "$test_arch" -fsanitize=address,undefined \
    tests/claude_profile_test.cpp -framework AppKit -lobjc -o "build/tests/claude-profile-$test_arch"
  arch -"$test_arch" "build/tests/claude-profile-$test_arch" "$PWD/build/tests/claude-profile-fixture-$test_arch"
done
