#!/bin/bash
# Build the separate GPL VLESS helper and its complete corresponding source archive.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
command -v go >/dev/null || { echo "Go 1.25.5 or newer is required for the VLESS helper." >&2; exit 1; }
mkdir -p build/arm64 build/x86_64
MODULE="$ROOT/tools/vless-helper"
MODULE_MODE=readonly
if [[ -f "$MODULE/vendor/modules.txt" ]]; then MODULE_MODE=vendor; fi
for PAIR in arm64:arm64 amd64:x86_64; do
  GOARCH_NAME="${PAIR%%:*}"
  MACH_ARCH="${PAIR##*:}"
  (
    cd "$MODULE"
    CGO_ENABLED=1 GOOS=darwin GOARCH="$GOARCH_NAME" \
      CGO_CFLAGS="-arch $MACH_ARCH -mmacosx-version-min=13.0" \
      CGO_LDFLAGS="-arch $MACH_ARCH -mmacosx-version-min=13.0" \
      go build -mod="$MODULE_MODE" -trimpath -tags with_utls -buildmode=pie \
      -ldflags="-s -w -linkmode external -extldflags=-mmacosx-version-min=13.0" \
      -o "$ROOT/build/$MACH_ARCH/appdeck-vless" .
  )
done
lipo -create build/arm64/appdeck-vless build/x86_64/appdeck-vless -output build/appdeck-vless

# Vendor only reachable dependencies. This staging directory never enters the repository.
STAGING="$(mktemp -d "$ROOT/build/vless-source.XXXXXX")"
trap 'rm -rf "$STAGING"' EXIT
mkdir -p "$STAGING/appdeck-vless-source/tools/vless-helper" "$STAGING/appdeck-vless-source/scripts"
cp "$MODULE"/*.go "$MODULE/go.mod" "$MODULE/go.sum" "$MODULE/LICENSE" "$MODULE/COPYING" "$MODULE/THIRD_PARTY_NOTICES.md" "$STAGING/appdeck-vless-source/tools/vless-helper/"
cp "$ROOT/scripts/build-vless.sh" "$STAGING/appdeck-vless-source/scripts/"
if [[ "$MODULE_MODE" == vendor ]]; then
  cp -R "$MODULE/vendor" "$STAGING/appdeck-vless-source/tools/vless-helper/vendor"
else
  (cd "$STAGING/appdeck-vless-source/tools/vless-helper" && go mod vendor)
fi
COPYFILE_DISABLE=1 tar -czf "$ROOT/build/appdeck-vless-source.tar.gz" -C "$STAGING" appdeck-vless-source
printf 'Built appdeck-vless (arm64 x86_64) and corresponding source archive.\n'
