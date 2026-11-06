# Claude profile storage and network setup

AppDeck 0.14 creates a clean Claude Desktop and Claude Code profile from the unchanged `/Applications/Claude.app`. Select **New profile → Clean Claude Desktop + Code**. The profile starts with new Desktop data and a private `CLAUDE_CONFIG_DIR`; no account, cookies, settings, plugins or transcripts are imported. Existing profiles keep their previous mode. This is local application storage isolation, not a new macOS user or a different physical computer. OS permissions, organization policies and settings inside a project still apply.

## Settings in the profile dialog

Choose a saved proxy, the AppDeck default, or a direct connection. When there is one saved proxy and no default, a new Claude profile selects that proxy. **Add…** opens the existing secure proxy editor. Existing profiles expose this dialog through **More → Claude profile settings…** while Claude and its linked browser are stopped.

| Setting | Effect and limits |
|---|---|
| Require proxy | Refuses a new launch if the selected proxy is missing or the local bridge cannot start. No direct launch is substituted. It is not a firewall for sockets opened by native helpers. |
| Block DNS bypass in linked Chrome | Adds host-resolver rules to the linked Chrome profile while a proxy is selected. Claude Desktop 4.4212.0 rejects this switch, so it is never passed to Claude. It does not intercept arbitrary Node/native/VM DNS requests. |
| Disable optional Code traffic and error reports | Sets the five switches below for the profile. Code auto-updates, experiments and Remote Control can become unavailable. This does not disable every Desktop request. |
| Also create a separate Chrome profile | Creates empty Chromium storage and a link to the Claude profile. **Open Claude in private Chrome…** opens `https://claude.ai/` through its selected route. Browser sign-in remains separate. |

If **Require proxy** is off, an absent default or a subsequently removed selection can resolve to a direct launch. If a selected proxy exists but its local bridge fails, launch is cancelled even with the checkbox off. The requirement checks configuration and bridge startup, not whether the upstream provider will accept CONNECT.

Each app process receives its own credential-free `http://127.0.0.1:<port>` bridge address. The bridge sends upstream proxy credentials from memory; passwords remain in AppDeck's `proxies.plist` (0600), never profile settings or process arguments. It exits when its app exits. The selected upstream may be HTTP CONNECT or SOCKS5; Claude Code itself receives HTTP because it does not directly support SOCKS proxies.

AppDeck sets upper- and lowercase `HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY`, `NO_PROXY` and `NODE_USE_ENV_PROXY=1`. `NO_PROXY` contains only localhost/loopback addresses. An HTTP URL for `HTTPS_PROXY` is intentional: the HTTP proxy transports TLS using CONNECT; TLS remains end to end. Claude also receives `CLAUDE_CODE_HTTP_PROXY`, `CLAUDE_CODE_HTTPS_PROXY`, `grpc_proxy` and `CLAUDE_CODE_PROXY_RESOLVES_HOSTS=1`. The aliases supplement the documented standard variables; they do not force every library to use a proxy.

The optional-traffic switches are `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1`, `DISABLE_TELEMETRY=1`, `DISABLE_ERROR_REPORTING=1`, `DISABLE_GROWTHBOOK=1` and `DO_NOT_TRACK=1`. The separate account-limit meter deliberately omits the aggregate switch: Claude Code otherwise skips its usage request. The meter uses the profile's proxy and an independent meter sign-in.

For new profiles with private Code storage, AppDeck also reconciles these allowlisted environment keys in that profile's `claude-code/settings.json`. This supports Code workers that read user settings instead of inheriting every Desktop environment variable. It preserves unrelated settings and keeps `settings.json.appdeck-previous`; malformed, linked or concurrently changed files cancel launch. It never edits global `~/.claude/settings.json`. The local bridge port is refreshed on each launch; a saved port is not a standalone proxy service after Claude quits.

Chromium receives `--proxy-server`, the loopback bypass list, `--disable-quic` and `--force-webrtc-ip-handling-policy=disable_non_proxied_udp`. The linked Chrome profile can additionally use `--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost, EXCLUDE 127.0.0.1, EXCLUDE [::1]`. Claude Desktop 4.4212.0 refuses startup with this switch; AppDeck leaves that vendor restriction intact. Code still receives its supported proxy-hostname-resolution setting. These control Chromium. Embedded Code, tools, MCP servers, SSH sessions, Cowork VMs and external browsers may use different networking. Managed organization policies remain authoritative.

Sources: [Claude Code network configuration](https://code.claude.com/docs/en/network-config), [environment variables](https://code.claude.com/docs/en/env-vars), [Desktop network requirements](https://code.claude.com/docs/en/desktop#network-access-requirements), [Chromium proxy behavior](https://chromium.googlesource.com/chromium/src/+/HEAD/net/docs/proxy.md), [Chromium resolver rules](https://www.chromium.org/developers/design-documents/network-stack/socks-proxy/).

## One VPS through Tailscale

Joining the Mac and VPS to the same tailnet makes the proxy reachable; it does not automatically route the Mac's Internet traffic through the VPS. Configure an HTTP proxy on the VPS bound to its Tailscale address, allow only the intended tailnet clients, and allow CONNECT to the required HTTPS destinations. Do not expose an unauthenticated open proxy on the public interface. Tinyproxy is one possible server; use the distribution's service management and its current configuration reference.

In AppDeck save the VPS Tailscale IP and proxy port, then select it in the Claude profile dialog. A numeric Tailscale address avoids needing local DNS to locate the proxy. The upstream resolves the destination hostnames sent in CONNECT. Use one upstream address if a stable egress IP is required: AppDeck's existing multi-address failover can change egress. The VPS provider, IPv4/IPv6 routing and proxy configuration must also keep egress stable.

Check both successful requests and failures: destination TLS must validate normally; temporarily unavailable proxy service must produce a connection failure; restarting a profile must update its local bridge port; the linked browser must use its own empty storage. Verify the remote address at a destination under your control and inspect DNS/UDP traffic at the enforcement boundary. A single successful request or two matching IP samples is not proof that all future traffic is contained. AppDeck's general Proxy-page check targets `api.openai.com`; it does not establish access to every Claude endpoint.

An optional Tailscale exit node routes traffic differently from an HTTP proxy. Tailnet ACLs restrict traffic inside Tailscale; they are not a general firewall for direct Internet traffic on the Mac. Tailscale itself may use STUN on UDP 3478 for tunnel discovery. Blocking browser WebRTC does not disable Tailscale's transport discovery.

Sources: [Tinyproxy](https://tinyproxy.github.io/), [Tinyproxy configuration reference](https://github.com/tinyproxy/tinyproxy/blob/master/etc/tinyproxy.conf.in), [Tailscale exit nodes](https://tailscale.com/docs/features/exit-nodes), [Tailscale ACL scope](https://tailscale.com/docs/features/access-control/acls), [Tailscale firewall ports](https://tailscale.com/docs/reference/faq/firewall-ports).

## hosts and complete egress enforcement

`/etc/hosts` is an exact-name mapping, not an outbound firewall. Mapping a name to `127.0.0.1` and `::1` affects clients that consult that local resolver. It does not block direct IP connections, other domain names, DNS-over-HTTPS, cached addresses or UDP. A local service on the mapped port can receive the connection. HTTP CONNECT can still work because the proxy receives and resolves the original destination name.

The proposed inventory includes the apex domains `anthropic.com`, `claude.ai` and `claude.com`, plus these names under each domain:

| Domain | Proposed subdomains, not an authoritative or complete vendor inventory |
|---|---|
| anthropic.com | www, api, console, auth, oauth, a-api, a-cdn, api-staging, statsig, mcp-proxy, mcp-proxy-staging, docs, support, status, portal, api-pr-preview |
| claude.ai | www, api, assets, preview, downloads |
| claude.com | www, api, code, platform, releases, docs, status, support, mcp, gcal.mcp, gmail.mcp, slack.mcp, microsoft365.mcp |

If an administrator uses this as a supplemental local resolver rule, each exact hostname needs both mappings. There is no wildcard subdomain rule in hosts. For example:

```text
127.0.0.1 api.anthropic.com
::1 api.anthropic.com
```

Maintain required destinations from the current official network documentation. Do not assume the proposed list is exhaustive or that every listed hostname currently exists. AppDeck does not edit hosts, install Tailscale, provision a VPS or change system firewall rules when creating a profile.

For a strict “only this proxy, otherwise no network” requirement, enforce default-deny egress outside the application: a managed OS networking boundary or a dedicated VM/network whose only allowed destination is the proxy. Cover IPv4, IPv6, TCP, UDP and DNS, including native helpers and child processes. Account for the Tailscale control and transport traffic required to reach the VPS; a blanket deny on the host can break that tunnel. Test failure behavior as well as successful requests at that boundary. AppDeck's profile switches cannot provide or verify this guarantee.

WebRTC/STUN can reveal address information when direct UDP is permitted. The Chromium policy above restricts non-proxied UDP; it is not evidence that every Claude version sends a STUN request. No such universal behavior was established in this release. See [RFC 8828: WebRTC IP address handling](https://www.rfc-editor.org/rfc/rfc8828).
