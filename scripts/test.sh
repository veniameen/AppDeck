#!/bin/bash
# Portable policy tests with AddressSanitizer/UndefinedBehaviorSanitizer, plus the localization
# check. Pure C++17: no AppKit, no profile data, no network. Used by CI.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-clang++}"
mkdir -p build/tests

python3 tools/check_i18n.py
for t in core_test edge_test workspace_filter_test usage_test proxy_test project_sync_test automation_sync_test; do
  "$CXX" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined "tests/$t.cpp" -o "build/tests/$t"
  "build/tests/$t"
done
