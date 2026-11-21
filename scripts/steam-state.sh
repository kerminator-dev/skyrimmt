# Steam's UI reports library readiness for both online and offline sessions. A running
# process alone isn't ready, and connection_log.txt never says Logged On offline.
# Pass the process start time (local ISO date) to reject a previous session's success.
steam_ui_state() {
  local since="$1"
  [ -f "$STEAM_DIR/logs/steamui_login.txt" ] || { echo starting; return; }
  awk -v since="$since" '
    BEGIN { state="starting"; offline=0 }
    substr($0,1,1)=="[" && substr($0,2,19)>=since {
      if (/Client version:/) { state="starting"; offline=0 }
      if (/Start offline - 1/ || /UI Request: go offline/) offline=1
      if (/Start offline - 0/) offline=0
      if (/SetLoginState:/) {
        state="starting"
        if (/SetLoginState: Success - OK/) state=offline ? "offline" : "online"
      }
    }
    END { print state }
  ' "$STEAM_DIR/logs/steamui_login.txt" 2>/dev/null || echo starting
}

steam_process_start() {
  local started
  started=$(LC_ALL=C ps -p "$1" -o lstart= 2>/dev/null)
  LC_ALL=C date -j -f '%a %b %e %T %Y' "$started" '+%Y-%m-%d %H:%M:%S' 2>/dev/null
}
