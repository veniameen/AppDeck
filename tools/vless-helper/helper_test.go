// SPDX-License-Identifier: GPL-3.0-or-later
package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/ecdh"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	box "github.com/sagernet/sing-box"
	boxtls "github.com/sagernet/sing-box/common/tls"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	servicevless "github.com/sagernet/sing-vmess/vless"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
	"github.com/sagernet/sing/service"
)

const fixtureUUID = "3e90c13d-727e-4365-aadc-8b60909e2b14"

func fixtureConfig() configuration {
	return configuration{Version: 1, Server: "127.0.0.1", Port: 443, UUID: fixtureUUID, Security: "tls", ServerName: "vless.test"}
}

func TestStrictConfiguration(t *testing.T) {
	valid := fixtureConfig()
	data, _ := json.Marshal(valid)
	if _, err := decodeConfiguration(data); err != nil {
		t.Fatal(err)
	}
	for name, modify := range map[string]func(*configuration){
		"version":        func(c *configuration) { c.Version = 2 },
		"port":           func(c *configuration) { c.Port = 65536 },
		"host injection": func(c *configuration) { c.Server = "host\nsecret" },
		"plaintext":      func(c *configuration) { c.Security = "none" },
		"sni":            func(c *configuration) { c.ServerName = "" },
		"uuid":           func(c *configuration) { c.UUID = "short" },
		"flow":           func(c *configuration) { c.Flow = "unsupported" },
		"fingerprint":    func(c *configuration) { c.Fingerprint = "custom" },
		"tls key":        func(c *configuration) { c.PublicKey = strings.Repeat("A", 43) },
		"reality key":    func(c *configuration) { c.Security = "reality"; c.PublicKey = "bad" },
	} {
		t.Run(name, func(t *testing.T) {
			c := valid
			modify(&c)
			b, _ := json.Marshal(c)
			if _, err := decodeConfiguration(b); err == nil {
				t.Fatal("accepted invalid configuration")
			}
		})
	}
	for _, bad := range [][]byte{
		append(data, []byte("{}")...),
		[]byte(strings.Replace(string(data), `"version":1`, `"version":1,"version":1`, 1)),
		[]byte(strings.Replace(string(data), `"version":1`, `"version":1,"insecure":true`, 1)),
	} {
		if _, err := decodeConfiguration(bad); err == nil {
			t.Fatal("accepted duplicate/unknown/trailing input")
		}
	}
	reality := valid
	reality.Security = "reality"
	reality.PublicKey = strings.Repeat("A", 43)
	reality.ShortID = "00ff"
	reality.Fingerprint = "chrome"
	reality.Flow = "xtls-rprx-vision"
	if !reality.valid() {
		t.Fatal("valid REALITY rejected")
	}
}

func testCertificate(t *testing.T) (tls.Certificate, string) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "vless.test"}, DNSNames: []string{"vless.test"}, IPAddresses: []net.IP{net.ParseIP("127.0.0.1")}, NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour), KeyUsage: x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}, IsCA: true, BasicConstraintsValid: true}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
	pair, err := tls.X509KeyPair(certPEM, pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER}))
	if err != nil {
		t.Fatal(err)
	}
	return pair, string(certPEM)
}

type testRelay struct{ accepted atomic.Int64 }

func (h *testRelay) NewConnectionEx(ctx context.Context, conn net.Conn, source, destination M.Socksaddr, onClose N.CloseHandlerFunc) {
	defer conn.Close()
	if onClose != nil {
		defer onClose(nil)
	}
	// Fail closed in the fixture: a test can never contact a public endpoint.
	if !destination.Addr.IsValid() || !destination.Addr.IsLoopback() {
		return
	}
	upstream, err := net.DialTimeout("tcp", destination.String(), 2*time.Second)
	if err != nil {
		return
	}
	defer upstream.Close()
	h.accepted.Add(1)
	done := make(chan struct{})
	go func() {
		io.Copy(upstream, conn)
		if tcp, ok := upstream.(*net.TCPConn); ok {
			tcp.CloseWrite()
		}
		close(done)
	}()
	io.Copy(conn, upstream)
	conn.Close()
	upstream.Close()
	<-done
}
func (*testRelay) NewPacketConnectionEx(_ context.Context, conn N.PacketConn, _, _ M.Socksaddr, onClose N.CloseHandlerFunc) {
	conn.Close()
	if onClose != nil {
		onClose(nil)
	}
}

func startRelay(t *testing.T, uuid string) (configuration, string, *testRelay, func()) {
	t.Helper()
	cert, pemText := testCertificate(t)
	listener, err := tls.Listen("tcp4", "127.0.0.1:0", &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS12})
	if err != nil {
		t.Fatal(err)
	}
	handler := &testRelay{}
	service := servicevless.NewService[string](log.NewNOPFactory().NewLogger(""), handler)
	service.UpdateUsers([]string{"fixture"}, []string{uuid}, []string{""})
	var connections sync.Map
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			connections.Store(conn, true)
			go func() {
				defer connections.Delete(conn)
				defer conn.Close()
				conn.SetDeadline(time.Now().Add(10 * time.Second))
				_ = service.NewConnection(context.Background(), conn, M.SocksaddrFromNet(conn.RemoteAddr()), nil)
			}()
		}
	}()
	var closeOnce sync.Once
	closeServer := func() {
		closeOnce.Do(func() { listener.Close(); connections.Range(func(k, v any) bool { k.(net.Conn).Close(); return true }) })
	}
	t.Cleanup(closeServer)
	c := fixtureConfig()
	c.Port = listener.Addr().(*net.TCPAddr).Port
	c.UUID = uuid
	return c, pemText, handler, closeServer
}

func testClient(t *testing.T, b *bridge, roots *x509.CertPool) *http.Client {
	t.Helper()
	u, _ := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", b.port()))
	transport := &http.Transport{Proxy: http.ProxyURL(u), TLSClientConfig: &tls.Config{RootCAs: roots}, DisableKeepAlives: true}
	t.Cleanup(transport.CloseIdleConnections)
	return &http.Client{Transport: transport, Timeout: 4 * time.Second}
}

func targetRoots(server *httptest.Server) *x509.CertPool {
	roots := x509.NewCertPool()
	roots.AddCert(server.Certificate())
	return roots
}

func TestIndependentProfilesAndTLSCheck(t *testing.T) {
	var requests atomic.Int64
	target := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { requests.Add(1); io.Copy(w, r.Body) }))
	defer target.Close()
	roots := targetRoots(target)
	var relays []*testRelay
	for i := 0; i < 2; i++ {
		c, pemText, relay, _ := startRelay(t, fixtureUUID)
		relays = append(relays, relay)
		b, err := newBridge(c, []string{pemText})
		if err != nil {
			t.Fatal(err)
		}
		defer b.close()
		if b.listener.Addr().(*net.TCPAddr).IP.String() != "127.0.0.1" {
			t.Fatal("listener not loopback")
		}
		body := bytes.Repeat([]byte{byte(40 + i)}, 1<<20)
		response, err := testClient(t, b, roots).Post(target.URL, "application/octet-stream", bytes.NewReader(body))
		if err != nil {
			t.Fatal(err)
		}
		got, err := io.ReadAll(response.Body)
		response.Body.Close()
		if err != nil || !bytes.Equal(got, body) {
			t.Fatal("tunnel corrupted payload")
		}
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		err = b.check(ctx, strings.TrimPrefix(target.URL, "https://"), roots)
		cancel()
		if err != nil {
			t.Fatal(err)
		}
	}
	if requests.Load() != 2 {
		t.Fatalf("TLS check sent HTTP requests: %d", requests.Load())
	}
	for _, relay := range relays {
		if relay.accepted.Load() != 2 {
			t.Fatalf("profile used unexpected relay: %d", relay.accepted.Load())
		}
	}
}

func TestPlainHTTPAndConcurrentTunnels(t *testing.T) {
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { fmt.Fprint(w, "through-vless") }))
	defer target.Close()
	c, pemText, relay, _ := startRelay(t, fixtureUUID)
	b, err := newBridge(c, []string{pemText})
	if err != nil {
		t.Fatal(err)
	}
	defer b.close()
	client := testClient(t, b, nil)
	var wg sync.WaitGroup
	for range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			response, err := client.Get(target.URL)
			if err != nil {
				t.Error(err)
				return
			}
			body, _ := io.ReadAll(response.Body)
			response.Body.Close()
			if string(body) != "through-vless" {
				t.Error("HTTP did not traverse relay")
			}
		}()
	}
	wg.Wait()
	if relay.accepted.Load() != 8 {
		t.Fatalf("relay count %d", relay.accepted.Load())
	}
}

func TestNoDirectFallback(t *testing.T) {
	var requests atomic.Int64
	target := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { requests.Add(1) }))
	defer target.Close()
	for _, mode := range []string{"wrong UUID", "untrusted certificate", "stopped server"} {
		t.Run(mode, func(t *testing.T) {
			c, pemText, _, stop := startRelay(t, fixtureUUID)
			trusted := []string{pemText}
			switch mode {
			case "wrong UUID":
				c.UUID = "12345678-1234-1234-1234-123456789abc"
			case "untrusted certificate":
				trusted = nil
			case "stopped server":
				stop()
			}
			b, err := newBridge(c, trusted)
			if err != nil {
				t.Fatal(err)
			}
			defer b.close()
			response, err := testClient(t, b, targetRoots(target)).Get(target.URL)
			if err == nil {
				response.Body.Close()
				t.Fatal("failed VLESS unexpectedly reached destination")
			}
		})
	}
	if requests.Load() != 0 {
		t.Fatalf("destination received %d requests without authenticated VLESS", requests.Load())
	}
}

func TestHelperSubprocess(t *testing.T) {
	if os.Getenv("APPDECK_VLESS_FIXTURE_PROCESS") != "1" {
		return
	}
	os.Exit(run(os.Stdin, os.Stdout, []string{"serve"}))
}

func startRealityRelay(t *testing.T, flow string) (configuration, *testRelay) {
	t.Helper()
	pair, _ := testCertificate(t)
	camouflage := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { fmt.Fprint(w, "fixture") }))
	camouflage.EnableHTTP2 = true
	camouflage.TLS = &tls.Config{Certificates: []tls.Certificate{pair}, MinVersion: tls.VersionTLS13}
	camouflage.StartTLS()
	t.Cleanup(camouflage.Close)
	camouflageAddr := camouflage.Listener.Addr().(*net.TCPAddr)
	privateKey, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	ctx := service.ContextWithDefaultRegistry(engineContext())
	engine, err := box.New(box.Options{Context: ctx, Options: option.Options{Log: &option.LogOptions{Disabled: true}}})
	if err != nil {
		t.Fatal(err)
	}
	if err = engine.Start(); err != nil {
		engine.Close()
		t.Fatal(err)
	}
	t.Cleanup(func() { engine.Close() })
	tlsOptions := option.InboundTLSOptions{Enabled: true, ServerName: "vless.test", Reality: &option.InboundRealityOptions{Enabled: true, PrivateKey: base64.RawURLEncoding.EncodeToString(privateKey.Bytes()), ShortID: []string{"00112233"}, Handshake: option.InboundRealityHandshakeOptions{ServerOptions: option.ServerOptions{Server: "127.0.0.1", ServerPort: uint16(camouflageAddr.Port)}}}}
	serverTLS, err := boxtls.NewRealityServer(ctx, log.NewNOPFactory().NewLogger(""), tlsOptions)
	if err != nil {
		t.Fatal(err)
	}
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { listener.Close() })
	handler := &testRelay{}
	vlessService := servicevless.NewService[string](log.NewNOPFactory().NewLogger(""), handler)
	vlessService.UpdateUsers([]string{"fixture"}, []string{fixtureUUID}, []string{flow})
	var connections sync.Map
	t.Cleanup(func() { connections.Range(func(k, v any) bool { k.(net.Conn).Close(); return true }) })
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			connections.Store(conn, true)
			go func() {
				defer connections.Delete(conn)
				defer conn.Close()
				conn.SetDeadline(time.Now().Add(8 * time.Second))
				secure, err := boxtls.ServerHandshake(ctx, conn, serverTLS)
				if err != nil {
					return
				}
				_ = vlessService.NewConnection(ctx, secure, M.SocksaddrFromNet(conn.RemoteAddr()), nil)
			}()
		}
	}()
	c := fixtureConfig()
	c.Port = listener.Addr().(*net.TCPAddr).Port
	c.Security = "reality"
	c.PublicKey = base64.RawURLEncoding.EncodeToString(privateKey.PublicKey().Bytes())
	c.ShortID = "00112233"
	c.Fingerprint = "chrome"
	c.Flow = flow
	return c, handler
}

func TestRealityVisionAndWrongKey(t *testing.T) {
	testRealityRoundtrip(t, "xtls-rprx-vision")
}

func TestRealityWithoutVisionAndWrongKey(t *testing.T) {
	testRealityRoundtrip(t, "")
}

func testRealityRoundtrip(t *testing.T, flow string) {
	var requests atomic.Int64
	target := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { requests.Add(1); fmt.Fprint(w, "reality-vision") }))
	defer target.Close()
	c, relay := startRealityRelay(t, flow)
	b, err := newBridge(c, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer b.close()
	response, err := testClient(t, b, targetRoots(target)).Get(target.URL)
	if err != nil {
		t.Fatal(err)
	}
	body, err := io.ReadAll(response.Body)
	response.Body.Close()
	if err != nil || string(body) != "reality-vision" {
		t.Fatalf("unexpected REALITY response: %q %v", body, err)
	}
	if relay.accepted.Load() != 1 {
		t.Fatal("request did not use REALITY relay")
	}
	wrongKey, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	c.PublicKey = base64.RawURLEncoding.EncodeToString(wrongKey.PublicKey().Bytes())
	wrong, err := newBridge(c, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer wrong.close()
	response, err = testClient(t, wrong, targetRoots(target)).Get(target.URL)
	if err == nil {
		response.Body.Close()
		t.Fatal("wrong REALITY key was accepted")
	}
	if requests.Load() != 1 {
		t.Fatal("wrong REALITY key fell back to direct destination")
	}
}

func startHelperProcess(t *testing.T) (*exec.Cmd, io.WriteCloser, *bufio.Reader, *bytes.Buffer) {
	t.Helper()
	cmd := exec.Command(os.Args[0], "-test.run=^TestHelperSubprocess$")
	cmd.Env = append(os.Environ(), "APPDECK_VLESS_FIXTURE_PROCESS=1")
	input, err := cmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	output, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	stderr := &bytes.Buffer{}
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cmd.Process.Kill() })
	return cmd, input, bufio.NewReader(output), stderr
}

func TestWatchLifecycleAndRedaction(t *testing.T) {
	c := fixtureConfig()
	data, _ := json.Marshal(c)
	for _, watched := range []bool{false, true} {
		t.Run(strconv.FormatBool(watched), func(t *testing.T) {
			cmd, input, output, stderr := startHelperProcess(t)
			fmt.Fprintln(input, string(data))
			ready, err := output.ReadString('\n')
			if err != nil || !strings.HasPrefix(ready, "port ") {
				t.Fatalf("not ready: %q %v", ready, err)
			}
			if watched {
				child := exec.Command("/bin/sleep", "30")
				if err := child.Start(); err != nil {
					t.Fatal(err)
				}
				t.Cleanup(func() { child.Process.Kill() })
				fmt.Fprintf(input, "watch %d\n", child.Process.Pid)
				input.Close()
				time.Sleep(150 * time.Millisecond)
				if cmd.Process.Signal(syscall.Signal(0)) != nil {
					t.Fatal("helper exited while watched process alive")
				}
				child.Process.Kill()
				child.Wait()
			} else {
				input.Close()
			}
			done := make(chan error, 1)
			go func() { done <- cmd.Wait() }()
			select {
			case err := <-done:
				if err != nil {
					t.Fatal(err)
				}
			case <-time.After(3 * time.Second):
				t.Fatal("helper outlived owner/pipe")
			}
			if stderr.Len() != 0 {
				t.Fatalf("unexpected stderr: %s", stderr.String())
			}
		})
	}
	var output bytes.Buffer
	if run(strings.NewReader(`{"version":1,"uuid":"secret-credential","insecure":true}`+"\n"), &output, nil) == 0 {
		t.Fatal("invalid input accepted")
	}
	if strings.Contains(output.String(), "secret") || output.String() != "fail 0 configuration\n" {
		t.Fatal("configuration echoed")
	}
	if processBirth(os.Getpid()) == 0 || processBirth(2147483647) != 0 {
		t.Fatal("process birth identity failed")
	}
}
