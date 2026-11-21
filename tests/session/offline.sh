#!/bin/bash
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export STEAM_DIR="$T"; mkdir -p "$T/logs"
. "$ROOT/scripts/steam-state.sh"
since='2026-11-17 12:00:00'
check() { local got; got=$(steam_ui_state "$since"); [ "$got" = "$1" ] || { echo "FAIL $2: $got"; exit 1; }; echo "PASS $2"; }
check starting 'missing log is not ready'
cat > "$T/logs/steamui_login.txt" <<'LOG'
[2026-11-16 12:00:00] Client version: 1
[2026-11-16 12:00:01] [ None ] Start offline - 0
[2026-11-16 12:00:02] [ WaitingForLibraryReady ] SetLoginState: Success - OK
LOG
check starting 'old online success is ignored'
cat >> "$T/logs/steamui_login.txt" <<'LOG'
[2026-11-17 12:00:01] Client version: 1
[2026-11-17 12:00:02] [ None ] Start offline - 1
[2026-11-17 12:00:03] [ None ] SetLoginState: WaitingForLibraryReady - OK
LOG
check starting 'offline selection alone is not readiness'
echo '[2026-11-17 12:00:04] [ WaitingForLibraryReady ] SetLoginState: Success - OK' >> "$T/logs/steamui_login.txt"
check offline 'offline library ready without Logged On'
echo '[2026-11-17 12:01:00] Client version: 1' >> "$T/logs/steamui_login.txt"
check starting 'restart invalidates previous success'
cat >> "$T/logs/steamui_login.txt" <<'LOG'
[2026-11-17 12:01:01] [ None ] Start offline - 0
[2026-11-17 12:01:02] [ WaitingForLibraryReady ] SetLoginState: Success - OK
LOG
check online 'online library ready'
echo '[2026-11-17 12:01:03] [ Success ] SetLoginState: LoginFailed - Error' >> "$T/logs/steamui_login.txt"
check starting 'failed login is not ready'
printf '[2026-11-17 12:02:00] [ None ] Start offline - 1\r\n[2026-11-17 12:02:01] [ WaitingForLibraryReady ] SetLoginState: Success - OK\r\n' > "$T/logs/steamui_login.txt"
check offline 'rotated CRLF log works'

# Exercise the actual launch gate without starting Wine or changing real files.
export SKYRIM_HOME="$T/home"; mkdir -p "$SKYRIM_HOME/config"; echo steam > "$SKYRIM_HOME/config/store"
set +eu
. "$ROOT/scripts/_env.sh"
set -eu
export STEAM_DIR="$T" SKYRIMMT_ROOT="$T/root"; mkdir -p "$SKYRIMMT_ROOT/scripts"
printf '#!/bin/bash\nexit 0\n' > "$SKYRIMMT_ROOT/scripts/steam.sh"; chmod +x "$SKYRIMMT_ROOT/scripts/steam.sh"
win_pids() { echo 123; }
steam_process_start() { echo '2026-11-17 12:00:00'; }
steam_running() { return 0; }
sleep() { :; }
ensure_steam | grep -q 'Steam ready (offline)'
echo 'PASS existing offline Steam passes launch gate'
win_pids() { :; }
printf '[%s] [ None ] Start offline - 1\n[%s] [ WaitingForLibraryReady ] SetLoginState: Success - OK\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$(date '+%Y-%m-%d %H:%M:%S')" > "$T/logs/steamui_login.txt"
ensure_steam | grep -q 'Steam ready (offline)'
echo 'PASS newly started offline Steam passes launch gate'
: > "$T/logs/steamui_login.txt"
if ensure_steam; then echo 'FAIL running process without login accepted'; exit 1; fi
echo 'PASS process alone is not accepted'
SKYRIM_STORE=gog
ensure_steam
echo 'PASS GOG needs no Steam'
