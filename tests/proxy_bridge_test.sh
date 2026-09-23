#!/bin/bash
# Offline acceptance of the proxy bridge (Contents/MacOS/appdeck-proxy): serve and check against the
# loopback fixtures of tests/proxy_fixture.py (a target server, an HTTP proxy with Basic auth, a SOCKS5
# proxy with a login). Nothing leaves the machine. Usage: tests/proxy_bridge_test.sh [AppDeck.app]
# APPDECK_PROXY_BIN=/path/appdeck-proxy tests another build of the helper (e.g. a sanitizer build).
set -uo pipefail
cd "$(dirname "$0")/.."
APP="${1:-build/AppDeck.app}"
BRIDGE="${APPDECK_PROXY_BIN:-$APP/Contents/MacOS/appdeck-proxy}"
[[ -x "$BRIDGE" ]] || { echo "FAIL: no proxy bridge at $BRIDGE"; exit 1; }
WORK="$(mktemp -d /private/tmp/appdeck-proxy-test.XXXXXX)"
PIDS=()
cleanup(){ exec 2>/dev/null; for p in "${PIDS[@]}"; do kill "$p"; done; rm -rf "$WORK"; } # quietly: no job reports
trap cleanup EXIT
FAILS=0; CHECKS=0
pass(){ CHECKS=$((CHECKS+1)); echo "PASS: $*"; }
fail(){ CHECKS=$((CHECKS+1)); FAILS=$((FAILS+1)); echo "FAIL: $*"; }
expect(){ local what="$1"; shift; if "$@"; then pass "$what"; else fail "$what"; fi; }

USER_NAME=alice; PASSWORD='pä55:w0rd' # the same pair as in tests/proxy_fixture.py
hex(){ printf %s "$1" | od -An -tx1 | tr -d ' \n'; }
UH="$(hex "$USER_NAME")"; PH="$(hex "$PASSWORD")"; WRONG="$(hex 'wrong password')"
# Explicit -x plus an empty --noproxy: the user's NO_PROXY must not route around the bridge.
PCURL=(curl -s --max-time 30 --noproxy '')
DCURL=(curl -s --max-time 10 --noproxy '*')

python3 tests/proxy_fixture.py "$WORK" > "$WORK/ports" 2> "$WORK/fixture.log" &
PIDS+=($!)
for _ in $(seq 100); do grep -q '^dead ' "$WORK/ports" 2>/dev/null && break; sleep 0.1; done
port(){ awk -v k="$1" '$1==k{print $2}' "$WORK/ports"; }
T=$(port target); T6=$(port target6); H=$(port http); S=$(port socks); SILENT=$(port silent); DEAD=$(port dead); HANGUP=$(port hangup)
[[ -n "$T" && -n "$DEAD" ]] || { cat "$WORK/fixture.log"; echo "FAIL: the proxy fixture did not start"; exit 1; }
BODY='appdeck-proxy-target'
seen(){ "${DCURL[@]}" "http://127.0.0.1:$T/stats" | python3 -c 'import json,sys;print(json.load(sys.stdin).get(sys.argv[1],0))' "$1"; }
sleep 600 & KEEP=$!; PIDS+=("$KEEP") # the process every long-running bridge watches

# start_bridge <name> <config lines...>: serve with that configuration; sets BPID and BPORT. The stdin
# writer then waits for <name>.go (its content is sent next, e.g. the watch line) and <name>.eof (closes stdin).
start_bridge(){
  local name="$1"; shift
  { printf '%s\n' "$@" ''
    until [[ -e "$WORK/$name.go" || ! -d "$WORK" ]]; do sleep 0.05; done; cat "$WORK/$name.go" 2>/dev/null
    until [[ -e "$WORK/$name.eof" || ! -d "$WORK" ]]; do sleep 0.05; done
  } | "$BRIDGE" serve > "$WORK/$name.out" 2> "$WORK/$name.err" &
  BPID=$!; PIDS+=("$BPID"); BPORT=""
  for _ in $(seq 100); do BPORT=$(awk '$1=="port"{print $2}' "$WORK/$name.out" 2>/dev/null); [[ -n "$BPORT" ]] && break; sleep 0.05; done
}
send(){ printf '%s' "$2" > "$WORK/$1.tmp"; mv "$WORK/$1.tmp" "$WORK/$1.go"; }
watch(){ send "$1" "watch $2"$'\n'; touch "$WORK/$1.eof"; } # the manager may close stdin right after the watch line
# finished <pid> <seconds>: the pipeline ends within the time; sets RC and ELAPSED.
finished(){ local start=$SECONDS; ( sleep "$2"; kill "$1" 2>/dev/null ) & local dog=$!; wait "$1"; RC=$?; ELAPSED=$((SECONDS-start)); kill "$dog" 2>/dev/null; wait "$dog" 2>/dev/null; }
# run_check <probe host:port> <config lines...>: the output of check; sets RC.
run_check(){ local probe="$1"; shift; OUT=$(printf '%s\n' "$@" '' | APPDECK_PROXY_PROBE="$probe" "$BRIDGE" check 2>>"$WORK/check.err"); RC=$?; }
same(){ [[ "$1" == "$2" ]]; }
matches(){ [[ "$1" =~ $2 ]]; }
get(){ "${PCURL[@]}" -x "http://127.0.0.1:$1" "http://127.0.0.1:$T/"; }        # a plain request through bridge port $1
tunnel_get(){ "${PCURL[@]}" -p -x "http://127.0.0.1:$1" "http://127.0.0.1:$T/"; } # the same through a CONNECT tunnel

# A bridge that gets its port line but no watch line: it must keep serving past the former 60 s limit
# (AppDeck may sit in an alert between the two lines). Checked at the end of the run.
start_bridge pending "scheme http" "endpoint 127.0.0.1 $H $UH $PH"; PENDING_PID=$BPID; PENDING_PORT=$BPORT; PENDING_AT=$SECONDS

# ---- HTTP upstream ----
start_bridge http "scheme http" "endpoint 127.0.0.1 $H $UH $PH"; watch http "$KEEP"; HTTP_PID=$BPID; HTTP_PORT=$BPORT
expect "serve prints a port" matches "$HTTP_PORT" '^[0-9]+$'
sleep 0.3 # stdin is closed now: the bridge must keep serving
expect "HTTP upstream: plain http:// request" same "$("${PCURL[@]}" -x "http://127.0.0.1:$HTTP_PORT" "http://127.0.0.1:$T/")" "$BODY"
expect "HTTP upstream: CONNECT tunnel" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$HTTP_PORT" "http://127.0.0.1:$T/")" "$BODY"
expect "HTTP upstream: CONNECT to a name" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$HTTP_PORT" "http://localhost:$T/")" "$BODY"
if [[ "$T6" != 0 ]]; then
  expect "HTTP upstream: CONNECT to [::1]" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$HTTP_PORT" "http://[::1]:$T6/")" "$BODY"
fi
expect "HTTP upstream saw the login on every request, never a wrong one" same "$(seen http_auth_bad)" 0
expect "HTTP upstream: absolute-form requests carry Connection: close" same "$(seen http_absolute_close)" "$(seen http_absolute)"
expect "only 127.0.0.1 listens (lsof)" same "$(lsof -nP -a -p "$HTTP_PID" -iTCP -sTCP:LISTEN -Fn | grep '^n')" "n127.0.0.1:$HTTP_PORT"
expect "stdin closed after watch: still serving" kill -0 "$HTTP_PID"
expect "not a web server: origin-form request gets 400" same "$("${DCURL[@]}" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HTTP_PORT/local")" 400

SHA=$(shasum -a 256 "$WORK/big.bin" | awk '{print $1}')
"${PCURL[@]}" -p -x "http://127.0.0.1:$HTTP_PORT" "http://127.0.0.1:$T/big" -o "$WORK/big.tunnel"
expect "8 MiB download through the tunnel is byte-exact" cmp -s "$WORK/big.bin" "$WORK/big.tunnel"
"${PCURL[@]}" -x "http://127.0.0.1:$HTTP_PORT" "http://127.0.0.1:$T/big" -o "$WORK/big.plain"
expect "8 MiB plain download is byte-exact" cmp -s "$WORK/big.bin" "$WORK/big.plain"
expect "8 MiB upload through the tunnel is byte-exact" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$HTTP_PORT" --data-binary @"$WORK/big.bin" "http://127.0.0.1:$T/echo")" "$SHA"
expect "8 MiB plain upload (body after the head) is byte-exact" same "$("${PCURL[@]}" -x "http://127.0.0.1:$HTTP_PORT" --data-binary @"$WORK/big.bin" "http://127.0.0.1:$T/echo")" "$SHA"

PAR=()
for i in $(seq 50); do
  if (( i % 2 )); then "${PCURL[@]}" -p -x "http://127.0.0.1:$HTTP_PORT" "http://127.0.0.1:$T/" > "$WORK/par.$i" & else "${PCURL[@]}" -x "http://127.0.0.1:$HTTP_PORT" "http://127.0.0.1:$T/" > "$WORK/par.$i" & fi
  PAR+=($!)
done
for p in "${PAR[@]}"; do wait "$p"; done
OKS=0; for i in $(seq 50); do [[ "$(cat "$WORK/par.$i")" == "$BODY" ]] && OKS=$((OKS+1)); done
expect "50 parallel requests (25 tunnels, 25 plain) all succeed ($OKS/50)" same "$OKS" 50

# ---- SOCKS5 upstream ----
start_bridge socks "scheme socks5" "endpoint 127.0.0.1 $S $UH $PH"; watch socks "$KEEP"; SOCKS_PORT=$BPORT
expect "SOCKS5 upstream: plain http:// request" same "$("${PCURL[@]}" -x "http://127.0.0.1:$SOCKS_PORT" "http://127.0.0.1:$T/")" "$BODY"
expect "SOCKS5 upstream: CONNECT tunnel" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$SOCKS_PORT" "http://127.0.0.1:$T/")" "$BODY"
expect "SOCKS5 upstream: a name goes as ATYP 3" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$SOCKS_PORT" "http://localhost:$T/")" "$BODY"
if [[ "$T6" != 0 ]]; then
  expect "SOCKS5 upstream: [::1] goes as ATYP 4" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$SOCKS_PORT" "http://[::1]:$T6/")$(seen socks_atyp4)" "${BODY}1"
fi
expect "SOCKS5 upstream saw the login, never a wrong one" same "$(( $(seen socks_auth_ok) >= 3 ))$(seen socks_auth_bad)" 10
HEADERS=$("${PCURL[@]}" -x "http://127.0.0.1:$SOCKS_PORT" "http://127.0.0.1:$T/headers")
expect "SOCKS5 plain request: origin form, Connection: close, no proxy headers" python3 -c '
import json,sys;h=json.loads(sys.argv[1]);assert h[0]=="GET /headers HTTP/1.1",h
assert "Connection: close" in h and not any(x.lower().startswith("proxy-") for x in h),h' "$HEADERS"
"${PCURL[@]}" -p -x "http://127.0.0.1:$SOCKS_PORT" "http://127.0.0.1:$T/big" -o "$WORK/big.socks"
expect "8 MiB download through a SOCKS5 tunnel is byte-exact" cmp -s "$WORK/big.bin" "$WORK/big.socks"
expect "8 MiB plain upload through SOCKS5 is byte-exact" same "$("${PCURL[@]}" -x "http://127.0.0.1:$SOCKS_PORT" --data-binary @"$WORK/big.bin" "http://127.0.0.1:$T/echo")" "$SHA"

# ---- failover and wrong logins ----
start_bridge failover "scheme http" "endpoint 127.0.0.1 $DEAD $UH $PH" "endpoint 127.0.0.1 $H $UH $WRONG" "endpoint localhost $H $UH $PH"; watch failover "$KEEP"
B0=$(seen http_auth_bad)
expect "failover: dead port and wrong login skipped (tunnel)" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$BPORT" "http://127.0.0.1:$T/")/$(( $(seen http_auth_bad)-B0 ))" "$BODY/1"
expect "failover: the endpoint that worked is tried first next time" same "$("${PCURL[@]}" -x "http://127.0.0.1:$BPORT" "http://127.0.0.1:$T/")$("${PCURL[@]}" -p -x "http://127.0.0.1:$BPORT" "http://127.0.0.1:$T/")/$(( $(seen http_auth_bad)-B0 ))" "$BODY$BODY/1"
start_bridge socksfo "scheme socks5" "endpoint 127.0.0.1 $DEAD $UH $PH" "endpoint 127.0.0.1 $S $UH $PH"; watch socksfo "$KEEP"
expect "failover: SOCKS5 dead port skipped" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$BPORT" "http://127.0.0.1:$T/")" "$BODY"
start_bridge wrong "scheme http" "endpoint 127.0.0.1 $H $UH $WRONG"; watch wrong "$KEEP"
expect "wrong password: tunnel gets 502" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$BPORT" -o /dev/null -w '%{http_connect}' "http://127.0.0.1:$T/")" 502
expect "wrong password: plain request gets 502, not the upstream's 407" same "$("${PCURL[@]}" -x "http://127.0.0.1:$BPORT" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$T/")" 502
start_bridge wrongsocks "scheme socks5" "endpoint 127.0.0.1 $S $UH $WRONG"; watch wrongsocks "$KEEP"
expect "SOCKS5 wrong password: tunnel gets 502" same "$("${PCURL[@]}" -p -x "http://127.0.0.1:$BPORT" -o /dev/null -w '%{http_connect}' "http://127.0.0.1:$T/")" 502
start_bridge deadonly "scheme http" "endpoint 127.0.0.1 $DEAD - -"; watch deadonly "$KEEP"
expect "no reachable upstream: 502" same "$("${PCURL[@]}" -x "http://127.0.0.1:$BPORT" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$T/")" 502

# Plain http:// requests fail over as tunnels do: each endpoint gets the head with its own login, and the
# endpoint that answers is tried first next time.
start_bridge plainfo "scheme http" "endpoint 127.0.0.1 $H $UH $WRONG" "endpoint 127.0.0.1 $H $UH $PH"; watch plainfo "$KEEP"
B0=$(seen http_auth_bad)
expect "plain failover: wrong login on endpoint 0, a plain request first (no tunnel before it) succeeds" same "$(get "$BPORT")/$(( $(seen http_auth_bad)-B0 ))" "$BODY/1"
expect "plain failover: the next plain request and tunnel go straight to endpoint 1" same "$(get "$BPORT")$(tunnel_get "$BPORT")/$(( $(seen http_auth_bad)-B0 ))" "$BODY$BODY/1"
start_bridge plainbody "scheme http" "endpoint 127.0.0.1 $H $UH $WRONG" "endpoint 127.0.0.1 $H $UH $PH"; watch plainbody "$KEEP"
B0=$(seen http_auth_bad)
CODE=$("${PCURL[@]}" -x "http://127.0.0.1:$BPORT" -H 'Expect:' --data-binary @"$WORK/big.bin" -o /dev/null -w '%{http_code}' "http://127.0.0.1:$T/echo")
expect "plain failover: a 407 after the body started streaming gets 502 (the body cannot be sent again)" same "$CODE" 502
expect "plain failover: after that 502 the next request starts at endpoint 1" same "$(get "$BPORT")/$(( $(seen http_auth_bad)-B0 ))" "$BODY/1"
start_bridge plainsilent "scheme http" "endpoint 127.0.0.1 $SILENT $UH $PH" "endpoint 127.0.0.1 $H $UH $PH"; watch plainsilent "$KEEP"
START=$SECONDS; GOT=$(get "$BPORT"); TOOK=$((SECONDS-START))
expect "plain failover: silent endpoint 0, the request succeeds on endpoint 1 after the 10 s limit (${TOOK}s)" same "$GOT/$(( TOOK >= 9 && TOOK <= 13 ))" "$BODY/1"
START=$SECONDS; GOT=$(get "$BPORT"); TOOK=$((SECONDS-START))
expect "plain failover: the next request goes straight to endpoint 1 (${TOOK}s)" same "$GOT/$(( TOOK <= 2 ))" "$BODY/1"
start_bridge plainhangup "scheme http" "endpoint 127.0.0.1 $HANGUP - -" "endpoint 127.0.0.1 $H $UH $PH"; watch plainhangup "$KEEP"
expect "plain failover: an endpoint that hangs up without an answer is skipped" same "$(get "$BPORT")" "$BODY"

# ---- check ----
run_check "127.0.0.1:$T" "scheme http" "endpoint 127.0.0.1 $H $UH $PH"
expect "check, HTTP upstream: ok 0 <ms> ($OUT)" matches "$OUT/$RC" '^ok 0 [0-9]+/0$'
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $S $UH $PH"
expect "check, SOCKS5 upstream: ok 0 <ms> ($OUT)" matches "$OUT/$RC" '^ok 0 [0-9]+/0$'
run_check "127.0.0.1:$T" "scheme http" "endpoint 127.0.0.1 $H $UH $WRONG"
expect "check, HTTP wrong password: auth 0" same "$OUT/$RC" "auth 0/1"
run_check "127.0.0.1:$T" "scheme http" "endpoint 127.0.0.1 $H - -"
expect "check, HTTP login required but none configured: auth 0" same "$OUT/$RC" "auth 0/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $S $UH $WRONG"
expect "check, SOCKS5 wrong password: auth 0" same "$OUT/$RC" "auth 0/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $S - -"
expect "check, SOCKS5 login required but none configured: auth 0" same "$OUT/$RC" "auth 0/1"
run_check "127.0.0.1:$T" "scheme http" "endpoint 127.0.0.1 $DEAD $UH $PH" "endpoint 127.0.0.1 $H $UH $PH"
expect "check, dead port then a good one: fail 0 refused, ok 1 ($(echo $OUT))" matches "$(echo $OUT)/$RC" '^fail 0 refused ok 1 [0-9]+/0$'
run_check "192.0.2.1:443" "scheme http" "endpoint 127.0.0.1 $H $UH $PH"
expect "check, upstream refuses the target: fail 0 status-403" same "$OUT/$RC" "fail 0 status-403/1"
run_check "192.0.2.1:443" "scheme socks5" "endpoint 127.0.0.1 $S $UH $PH"
expect "check, SOCKS5 refuses the target: fail 0 socks-2" same "$OUT/$RC" "fail 0 socks-2/1"
# Only a SOCKS5 server's own answers are login problems; anything else on a SOCKS port is a protocol error.
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port http400) $UH $PH"
expect "check, SOCKS5 greeting answered by an HTTP server: fail 0 protocol, not auth" same "$OUT/$RC" "fail 0 protocol/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port tlsalert) - -"
expect "check, SOCKS5 greeting answered with a TLS alert: fail 0 protocol" same "$OUT/$RC" "fail 0 protocol/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port socksgss) $UH $PH"
expect "check, SOCKS5 server picks a method never offered: fail 0 protocol" same "$OUT/$RC" "fail 0 protocol/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port socksinsist) - -"
expect "check, SOCKS5 server insists on a login none is configured for: auth 0" same "$OUT/$RC" "auth 0/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port socksv5auth) $UH $PH"
expect "check, SOCKS5 login accepted with version byte 5 (as curl accepts it): ok 0 ($OUT)" matches "$OUT/$RC" '^ok 0 [0-9]+/0$'
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port socksauthodd) $UH $PH"
expect "check, SOCKS5 login answered with garbage: fail 0 protocol" same "$OUT/$RC" "fail 0 protocol/1"
run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $(port socksrep4) - -"
expect "check, SOCKS5 refusal with an unknown address type: fail 0 socks-4" same "$OUT/$RC" "fail 0 socks-4/1"
START=$SECONDS; run_check "127.0.0.1:$T" "scheme socks5" "endpoint 127.0.0.1 $SILENT $UH $PH"
expect "check, silent upstream: fail 0 timeout after the 10 s handshake limit" same "$OUT/$RC/$(( SECONDS-START >= 9 && SECONDS-START <= 13 ))" "fail 0 timeout/1/1"

# ---- configuration errors: exit 2, one line, no credential ----
bad_config(){ OUT=$(printf '%s\n' "$@" '' | "$BRIDGE" check 2>&1 >/dev/null); RC=$?; [[ $RC == 2 && $(printf '%s\n' "$OUT" | wc -l) -eq 1 && "$OUT" != *"$PH"* && "$OUT" != *"$UH"* && "$OUT" != *w0rd* ]]; }
expect "bad config: unknown scheme" bad_config "scheme ftp" "endpoint 127.0.0.1 $H $UH $PH"
expect "bad config: no scheme" bad_config "endpoint 127.0.0.1 $H $UH $PH"
expect "bad config: no endpoint" bad_config "scheme http"
expect "bad config: odd or non-hex credentials" bad_config "scheme http" "endpoint 127.0.0.1 $H ${UH}0 zz$PH"
expect "bad config: NUL byte in a password" bad_config "scheme http" "endpoint 127.0.0.1 $H $UH 00$PH"
expect "bad config: password without login" bad_config "scheme http" "endpoint 127.0.0.1 $H - $PH"
expect "bad config: port 0" bad_config "scheme http" "endpoint 127.0.0.1 0 $UH $PH"
expect "bad config: host with a slash" bad_config "scheme http" "endpoint a/b 80 $UH $PH"
expect "bad config: unknown directive" bad_config "scheme http" "endpoint 127.0.0.1 $H $UH $PH" "password $PH"
expect "bad config: extra field" bad_config "scheme http" "endpoint 127.0.0.1 $H $UH $PH x"
MANY=(); for i in $(seq 33); do MANY+=("endpoint 127.0.0.1 $H $UH $PH"); done
expect "bad config: 33 endpoints" bad_config "scheme http" "${MANY[@]}"
expect "bad usage: no mode" bash -c '"$0" </dev/null 2>/dev/null; [[ $? == 2 ]]' "$BRIDGE"

# ---- lifecycle ----
sleep 2 & SHORT=$!
start_bridge life "scheme http" "endpoint 127.0.0.1 $H $UH $PH"; watch life "$SHORT"
expect "serves while the watched process lives" same "$("${PCURL[@]}" -x "http://127.0.0.1:$BPORT" "http://127.0.0.1:$T/")" "$BODY"
finished "$BPID" 8
expect "exits 0 when the watched process exits (${ELAPSED}s)" same "$RC/$(( ELAPSED <= 5 ))" "0/1"
sleep 0 & GONE=$!; wait "$GONE"
start_bridge gone "scheme http" "endpoint 127.0.0.1 $H $UH $PH"; watch gone "$GONE"; finished "$BPID" 5
expect "exits at once when the watched process is already gone (${ELAPSED}s)" same "$RC/$(( ELAPSED <= 3 ))" "0/1"
start_bridge nowatch "scheme http" "endpoint 127.0.0.1 $H $UH $PH"; send nowatch ""; touch "$WORK/nowatch.eof"; finished "$BPID" 5
expect "exits 0 when stdin closes before the watch line (${ELAPSED}s)" same "$RC/$(( ELAPSED <= 3 ))/$(cat "$WORK/nowatch.out")" "0/1/port $BPORT"
start_bridge badwatch "scheme http" "endpoint 127.0.0.1 $H $UH $PH"; send badwatch "watch me"$'\n'; touch "$WORK/badwatch.eof"; finished "$BPID" 5
expect "exits 2 on a malformed watch line" same "$RC" 2
while (( SECONDS-PENDING_AT < 62 )); do sleep 1; done
expect "no watch line $((SECONDS-PENDING_AT)) s after the port line: still serving" same "$(kill -0 "$PENDING_PID" && get "$PENDING_PORT")" "$BODY"
send pending ""; touch "$WORK/pending.eof"; finished "$PENDING_PID" 5
expect "then exits 0 at once when stdin closes (${ELAPSED}s)" same "$RC/$(( ELAPSED <= 2 ))" "0/1"

# ---- no secrets anywhere ----
LEAK=0; for f in "$WORK"/*.err "$WORK"/*.out; do grep -aqF -e "$PH" -e "$UH" -e "w0rd" "$f" && LEAK=1; done
expect "no credential on stdout/stderr of any run" same "$LEAK" 0
expect "the bridge's argv carries no credential" bash -c '! ps -o args= -p "$1" | grep -qF -e w0rd -e "$2"' _ "$HTTP_PID" "$PH"

if (( FAILS )); then echo "FAIL: proxy bridge: $FAILS of $CHECKS checks failed"; exit 1; fi
echo "PASS: proxy bridge: $CHECKS checks (serve, check, failover, lifecycle, loopback only)"
