// SPDX-License-Identifier: GPL-3.0-or-later
package main

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"net"
	"sync"
	"time"

	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/certificate"
	"github.com/sagernet/sing-box/adapter/endpoint"
	"github.com/sagernet/sing-box/adapter/inbound"
	"github.com/sagernet/sing-box/adapter/outbound"
	boxservice "github.com/sagernet/sing-box/adapter/service"
	"github.com/sagernet/sing-box/dns"
	"github.com/sagernet/sing-box/dns/transport/local"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	httpin "github.com/sagernet/sing-box/protocol/http"
	"github.com/sagernet/sing-box/protocol/vless"
	"github.com/sagernet/sing/common/json/badoption"
	M "github.com/sagernet/sing/common/metadata"
)

type bridge struct {
	engine      *box.Box
	listener    net.Listener
	cancel      context.CancelFunc
	lock        sync.Mutex
	connections map[net.Conn]struct{}
	closed      bool
}

func engineContext() context.Context {
	ins := inbound.NewRegistry()
	outs := outbound.NewRegistry()
	vless.RegisterOutbound(outs)
	transports := dns.NewTransportRegistry()
	local.RegisterTransport(transports)
	return box.Context(context.Background(), ins, outs, endpoint.NewRegistry(), transports, boxservice.NewRegistry(), certificate.NewRegistry())
}

// roots is only used by in-process tests. Production stdin has no trust override.
func newBridge(c configuration, roots []string) (*bridge, error) {
	if !c.valid() {
		return nil, errConfiguration
	}
	ctx, cancel := context.WithCancel(engineContext())
	tlsOptions := &option.OutboundTLSOptions{Enabled: true, ServerName: c.ServerName, MinVersion: "1.2", Certificate: roots}
	if c.Fingerprint != "" || c.Security == "reality" {
		fingerprint := c.Fingerprint
		if fingerprint == "" {
			fingerprint = "chrome"
		}
		tlsOptions.UTLS = &option.OutboundUTLSOptions{Enabled: true, Fingerprint: fingerprint}
	}
	if c.Security == "reality" {
		tlsOptions.Reality = &option.OutboundRealityOptions{Enabled: true, PublicKey: c.PublicKey, ShortID: c.ShortID}
	}
	vlessOptions := &option.VLESSOutboundOptions{
		ServerOptions: option.ServerOptions{Server: c.Server, ServerPort: uint16(c.Port)},
		UUID:          c.UUID, Flow: c.Flow,
		OutboundTLSOptionsContainer: option.OutboundTLSOptionsContainer{TLS: tlsOptions},
		DialerOptions:               option.DialerOptions{AbstractDialerOptions: option.AbstractDialerOptions{ConnectTimeout: badoption.Duration(15 * time.Second)}},
	}
	engine, err := box.New(box.Options{Context: ctx, Options: option.Options{
		Log:       &option.LogOptions{Disabled: true},
		DNS:       &option.DNSOptions{RawDNSOptions: option.RawDNSOptions{Servers: []option.DNSServerOptions{{Type: "local", Tag: "local", Options: &option.LocalDNSServerOptions{}}}, Final: "local"}},
		Route:     &option.RouteOptions{Final: "vless", DefaultDomainResolver: &option.DomainResolveOptions{Server: "local"}},
		Outbounds: []option.Outbound{{Type: "vless", Tag: "vless", Options: vlessOptions}},
	}})
	if err != nil {
		cancel()
		return nil, errors.New("engine initialization failed")
	}
	if err = engine.Start(); err != nil {
		engine.Close()
		cancel()
		return nil, errors.New("engine startup failed")
	}
	// The HTTP implementation is injected on our listener, so an ephemeral port is
	// acquired once and remains bound. No reserve-close-rebind race or system proxy.
	rawInbound, err := httpin.NewInbound(ctx, engine.Router(), log.NewNOPFactory().NewLogger(""), "loopback", option.HTTPMixedInboundOptions{})
	if err != nil {
		engine.Close()
		cancel()
		return nil, errors.New("inbound initialization failed")
	}
	injector, ok := rawInbound.(adapter.TCPInjectableInbound)
	if !ok {
		engine.Close()
		cancel()
		return nil, errors.New("inbound unavailable")
	}
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		engine.Close()
		cancel()
		return nil, errors.New("loopback unavailable")
	}
	b := &bridge{engine: engine, listener: listener, cancel: cancel, connections: make(map[net.Conn]struct{})}
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			b.lock.Lock()
			if b.closed {
				b.lock.Unlock()
				conn.Close()
				return
			}
			b.connections[conn] = struct{}{}
			b.lock.Unlock()
			go injector.NewConnection(ctx, conn, adapter.InboundContext{Source: M.SocksaddrFromNet(conn.RemoteAddr())}, func(error) {
				conn.Close()
				b.lock.Lock()
				delete(b.connections, conn)
				b.lock.Unlock()
			})
		}
	}()
	return b, nil
}

func (b *bridge) port() int { return b.listener.Addr().(*net.TCPAddr).Port }
func (b *bridge) close() {
	b.lock.Lock()
	if b.closed {
		b.lock.Unlock()
		return
	}
	b.closed = true
	b.listener.Close()
	for conn := range b.connections {
		conn.Close()
	}
	b.lock.Unlock()
	b.cancel()
	b.engine.Close()
}

// This sends only a TLS handshake to the target through VLESS. It proves more
// than DialContext, which may return before the VLESS server validates its UUID.
func (b *bridge) check(ctx context.Context, destination string, roots *x509.CertPool) error {
	out, ok := b.engine.Outbound().Outbound("vless")
	if !ok {
		return errors.New("unavailable")
	}
	addr := M.ParseSocksaddr(destination)
	conn, err := out.DialContext(ctx, "tcp", addr)
	if err != nil {
		return errors.New("connection")
	}
	defer conn.Close()
	if deadline, ok := ctx.Deadline(); ok {
		conn.SetDeadline(deadline)
	}
	tlsConn := tls.Client(conn, &tls.Config{ServerName: addr.AddrString(), RootCAs: roots, MinVersion: tls.VersionTLS12})
	if err := tlsConn.HandshakeContext(ctx); err != nil {
		return errors.New("connection")
	}
	return nil
}
