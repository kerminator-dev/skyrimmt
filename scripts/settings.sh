#!/bin/bash
# Game settings that SkyrimMT manages, changed ONLY when the player picks something in the launcher (never on Play;
# PLAN §4.8). Skyrim.ini keys go to SkyrimCustom.ini (it overrides Skyrim.ini); SkyrimPrefs.ini keys must be written
# into SkyrimPrefs.ini itself (SkyrimCustom.ini does not override them — measured, docs/m1-report.md).
#   settings.sh status                    key=value lines for the launcher
#   settings.sh pacing 30|45|60|unlocked  DXMT cap and Skyrim's own 60 fps lock
#   settings.sh preset low|medium|high|ultra   Bethesda's preset values (the game's own Low/Medium/High/Ultra.ini)
#   settings.sh gamepad on|off            Skyrim's controller support (SkyrimPrefs [Main] bGamepadEnable)
#   settings.sh audio native|builtin      Microsoft XAudio2 (correct sound distances, default) or Wine's own
#   settings.sh shaders native|builtin    Microsoft's shader compiler (Community Shaders, default) or Wine's own
. "$(dirname "$0")/_env.sh"; . "$(dirname "$0")/ini.sh"
CUSTOM="$MYGAMES_DIR/SkyrimCustom.ini"; PREFS="$MYGAMES_DIR/SkyrimPrefs.ini"
DT="$GAMEDIR/Data/SKSE/Plugins/SSEDisplayTweaks.dll"

game_version() {   # PE VS_FIXEDFILEINFO of SkyrimSE.exe -> 1.7.104.0
  local f="$GAMEDIR/SkyrimSE.exe" off
  [ -f "$f" ] || return 1
  off=$(LC_ALL=C grep -obUa $'\xbd\x04\xef\xfe' "$f" | LC_ALL=C head -1 | LC_ALL=C cut -d: -f1)
  [ -n "$off" ] || return 1
  set -- $(od -An -tu2 -j $((off + 8)) -N8 "$f"); echo "$2.$1.$4.$3"
}
skse_dll() { ls "$GAMEDIR"/skse64_1_*.dll 2>/dev/null | head -1; }   # e.g. skse64_1_7_104.dll = game 1.7.104

preset_now() {   # which Bethesda preset all of SkyrimPrefs' preset keys currently match, else "custom"
  local q f k v cur ok sec
  [ -f "$PREFS" ] || { echo none; return; }
  for q in Mac Low Medium High Ultra; do
    f="$GAMEDIR/$q.ini"; [ "$q" = Mac ] && f="$SKYRIMMT_ROOT/config/Mac.ini"; [ -f "$f" ] || continue; ok=1; sec=""
    while IFS= read -r line; do
      line=${line%$'\r'}
      case $line in \[*\]) sec=${line#[}; sec=${sec%]}; continue ;; *=*) ;; *) continue ;; esac
      k=${line%%=*}; v=${line#*=}
      cur=$(ini_get "$PREFS" "$sec" "$k") || { ok=0; break; }
      # numbers: 8000 == 8000.0000
      if [ "$cur" != "$v" ] && ! awk -v a="$cur" -v b="$v" 'BEGIN { exit !(a + 0 == b + 0 && a ~ /^[-0-9.]+$/ && b ~ /^[-0-9.]+$/) }'; then ok=0; break; fi
    done < "$f"
    [ $ok = 1 ] && { echo "$q" | tr 'A-Z' 'a-z'; return; }
  done
  echo custom
}

case "${1:-status}" in
  status)
    lock=$(ini_get "$CUSTOM" Display bLockFrameRate 2>/dev/null)
    limit=$(dxmt_frame_limit)
    if [ -n "$limit" ]; then echo "pacing=$limit"
    elif [ "$lock" = 0 ]; then echo "pacing=unlocked"
    else echo "pacing=60"; fi
    echo "preset=$(preset_now)"
    echo "game_version=$(game_version 2>/dev/null)"
    d=$(skse_dll); v=${d##*/skse64_}; v=${v%.dll}
    echo "skse=$([ -f "$GAMEDIR/skse64_loader.exe" ] && echo 1 || echo 0) skse_for=$(echo "$v" | tr _ .)"
    echo "displaytweaks=$([ -f "$DT" ] && echo 1 || echo 0)"
    p="$LOCALAPPDATA_DIR/plugins.txt"
    n=$(grep -c '^\*' "$p" 2>/dev/null); echo "plugins_active=${n:-0}"
    echo "gamepad=$(ini_get "$PREFS" Main bGamepadEnable 2>/dev/null || echo 0)"   # Skyrim's controller switch
    echo "audio=$(audio_mode)"
    echo "shaders=$(cat "$SKYRIM_HOME/config/d3dcompiler" 2>/dev/null || echo native)"
    ;;
  gamepad)   # SkyrimPrefs [Main] bGamepadEnable (same as Settings > Gameplay > Controller in game)
    case ${2:-} in on) v=1 ;; off) v=0 ;; *) echo "usage: settings.sh gamepad on|off"; exit 2 ;; esac
    game_running && { echo "quit Skyrim first (it rewrites SkyrimPrefs.ini on exit)"; exit 1; }
    [ -f "$PREFS" ] || { echo "SkyrimPrefs.ini not found — start Skyrim once"; exit 1; }
    ini_set "$PREFS" Main bGamepadEnable "$v" && echo "controller $2" ;;
  pacing)
    case ${2:-} in
      30|45|60)
        ini_del "$CUSTOM" Display bLockFrameRate || exit 1
        if [ "$2" = 60 ]; then
          rm -f "$SKYRIM_HOME/config/frame-limit" || exit 1
        else
          mkdir -p "$SKYRIM_HOME/config" || exit 1
          temp=$(mktemp "$SKYRIM_HOME/config/frame-limit.XXXXXX") || exit 1
          trap 'rm -f "$temp"' EXIT
          printf '%s\n' "$2" > "$temp" && mv -f "$temp" "$SKYRIM_HOME/config/frame-limit" || exit 1
        fi
        echo "$2 fps limit selected" ;;
      unlocked)
        [ -f "$DT" ] || [ "${SKYRIMMT_FORCE:-0}" = 1 ] || { echo "needs SSE Display Tweaks (physics above 60 fps)"; exit 1; }
        ini_set "$CUSTOM" Display bLockFrameRate 0 && rm -f "$SKYRIM_HOME/config/frame-limit" || exit 1
        echo "60 fps lock off" ;;
      *) echo "usage: settings.sh pacing 30|45|60|unlocked"; exit 2 ;;
    esac ;;
  preset)
    q=$(echo "${2:-}" | tr 'a-z' 'A-Z' | cut -c1)$(echo "${2:-}" | cut -c2- | tr 'A-Z' 'a-z')
    case "$q" in Low|Medium|High|Ultra|Mac) ;; *) echo "unknown preset"; exit 2 ;; esac
    f="$GAMEDIR/$q.ini"; [ "$q" = Mac ] && f="$SKYRIMMT_ROOT/config/Mac.ini"
    [ -f "$f" ] || { echo "unknown preset ${2:-}"; exit 2; }
    game_running || loader_running && { echo "quit Skyrim and its graphics launcher first"; exit 1; }
    [ -f "$PREFS" ] || { mkdir -p "$MYGAMES_DIR"; cp "$GAMEDIR/Skyrim/SkyrimPrefs.ini" "$PREFS" 2>/dev/null || : > "$PREFS"; }
    game_running && { echo "quit Skyrim first (it rewrites SkyrimPrefs.ini on exit)"; exit 1; }
    backup="$SKYRIM_HOME/backups/before-preset.$(date +%Y%m%d-%H%M%S).$$"
    mkdir -p "$backup" || exit 1
    cp -p "$PREFS" "$backup/SkyrimPrefs.ini" || exit 1
    [ ! -f "$CUSTOM" ] || cp -p "$CUSTOM" "$backup/SkyrimCustom.ini" || exit 1
    [ ! -f "$SKYRIM_HOME/config/frame-limit" ] || cp -p "$SKYRIM_HOME/config/frame-limit" "$backup/frame-limit" || exit 1
    ini_merge "$PREFS" "$f" || exit 1
    if [ "$q" = Mac ]; then
      bash "$SKYRIMMT_ROOT/scripts/settings.sh" pacing 30 || exit 1
    fi
    echo "$q preset applied (backup: $backup)" ;;
  audio)     # native = Microsoft XAudio2 for SkyrimSE.exe, builtin = Wine's FAudio (applied now and at every Play)
    case ${2:-} in native|builtin) ;; *) echo "usage: settings.sh audio native|builtin"; exit 2 ;; esac
    mkdir -p "$SKYRIM_HOME/config"; echo "$2" > "$SKYRIM_HOME/config/audio"
    ensure_audio_libs; echo "OK audio=$2" ;;
  shaders)   # native = Microsoft d3dcompiler_47 for SkyrimSE.exe, builtin = Wine's (vkd3d-shader)
    case ${2:-} in native|builtin) ;; *) echo "usage: settings.sh shaders native|builtin"; exit 2 ;; esac
    mkdir -p "$SKYRIM_HOME/config"; echo "$2" > "$SKYRIM_HOME/config/d3dcompiler"
    ensure_shader_compiler; echo "OK shaders=$2" ;;
  *) echo "usage: settings.sh status|pacing|preset|gamepad|audio|shaders"; exit 2 ;;
esac
