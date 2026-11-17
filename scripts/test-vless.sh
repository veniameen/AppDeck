#!/bin/bash
# Offline VLESS/TLS loopback and lifecycle fixtures. No vendor/profile/network settings access.
set -euo pipefail
cd "$(dirname "$0")/../tools/vless-helper"
go test -mod=readonly -tags with_utls -timeout 90s ./...
# Upstream Vision converts reflected TLS pointers through uintptr (vision.go:90),
# which Go's race-implied checkptr rejects. Keep its real roundtrip in the full
# ordinary run above; race-check every other fixture, including REALITY itself.
go test -mod=readonly -race -tags with_utls -skip '^TestRealityVisionAndWrongKey$' -timeout 90s ./...
