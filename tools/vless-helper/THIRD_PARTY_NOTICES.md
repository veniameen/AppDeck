# AppDeck VLESS helper

Copyright (c) 2026 Veniamin Ryadnov. This separate executable is licensed under GPL-3.0-or-later; the full license is in COPYING. The AppDeck C++ application remains MIT-licensed. The helper is an independent derivative and is not an official SagerNet product.

It uses sing-box v1.14.4 (Copyright 2022 nekohasekai), licensed under GPL-3.0-or-later, and its pinned dependencies listed in go.mod and go.sum. The corresponding source archive shipped alongside the executable contains this helper's sources, its build script, and dependency sources with their upstream license notices under vendor/.

The build intentionally registers only the VLESS outbound and local DNS transport. HTTP parsing uses the upstream HTTP inbound injected onto a loopback-only TCP listener. It does not create a TUN interface, change system routing or DNS settings, install a service, or set the system proxy. Per-profile routing has the same client-support limits as AppDeck's HTTP proxy mode.

Only a strict JSON object on stdin can provide endpoint configuration. The helper never accepts configurations in argv or environment. It disables upstream logs, does not log destinations or identities, and emits only protocol status categories and a loopback port. The check command performs a verified TLS handshake through VLESS to api.openai.com:443 and does not send an HTTP request or use account credentials.

Supported scope: VLESS over TCP with TLS or REALITY, optional xtls-rprx-vision, and an allowlisted uTLS fingerprint. TLS verification cannot be disabled through the protocol. WebSocket, gRPC, XHTTP, subscription fetching, arbitrary sing-box configuration and direct fallback are not accepted.

Validation: the ordinary offline suite includes real TLS and REALITY/Vision loopback roundtrips. The race suite includes REALITY without Vision; its Vision case runs separately without race instrumentation because the pinned upstream Vision implementation uses uintptr-based access to TLS buffers that Go checkptr rejects. No global checkptr suppression or upstream cryptography patch is used.

Sources: https://github.com/SagerNet/sing-box/tree/v1.14.4 and https://sing-box.sagernet.org/ .
