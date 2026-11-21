#!/bin/bash
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export SKYRIMMT_ROOT="$T" SKYRIM_HOME="$T/home" GAMEDIR="$T/game" BASE_GAMEDIR="$T/base" MYGAMES_DIR="$T/prefs"
mkdir -p "$T/scripts" "$GAMEDIR" "$BASE_GAMEDIR" "$MYGAMES_DIR" "$SKYRIM_HOME/config"
cp "$ROOT/scripts/graphics-launcher.sh" "$ROOT/scripts/ini.sh" "$T/scripts/"
cat > "$T/scripts/_env.sh" <<'SH'
game_running() { [ "${TEST_BUSY:-0}" = 1 ]; }
loader_running() { return 1; }
ensure_steam() { return 0; }
SH
cat > "$T/scripts/run-exe.sh" <<'SH'
#!/bin/bash
printf '%s' "$2" > "$SKYRIM_HOME/exe-used"
if [ "${TEST_EDIT:-0}" = 1 ]; then
  printf '[Display]\niSize W=1280\niSize H=800\nfShadowDistance=3000\n' > "$MYGAMES_DIR/SkyrimPrefs.ini"
fi
exit "${TEST_RC:-0}"
SH
chmod +x "$T/scripts/run-exe.sh"
: > "$BASE_GAMEDIR/SkyrimSELauncher.exe"
printf '[Display]\niSize W=1440\niSize H=932\nfShadowDistance=8000\n' > "$MYGAMES_DIR/SkyrimPrefs.ini"
echo fit > "$SKYRIM_HOME/config/window-size"
bash "$T/scripts/graphics-launcher.sh" >/dev/null
[ "$(cat "$SKYRIM_HOME/config/window-size")" = fit ]
[ "$(cat "$SKYRIM_HOME/exe-used")" = "$BASE_GAMEDIR/SkyrimSELauncher.exe" ]
echo 'PASS cancel preserves resolution and pinned install falls back to base launcher'
TEST_EDIT=1 bash "$T/scripts/graphics-launcher.sh" >/dev/null
[ "$(cat "$SKYRIM_HOME/config/window-size")" = original ]
grep -q 'fShadowDistance=3000' "$MYGAMES_DIR/SkyrimPrefs.ini"
grep -q 'fShadowDistance=8000' "$SKYRIM_HOME"/backups/*/SkyrimPrefs.ini
[ ! -f "$SKYRIM_HOME/run/launch-pending" ]
echo 'PASS changed resolution adopted, graphics preserved, previous INI backed up, launch marker cleared'
if TEST_BUSY=1 bash "$T/scripts/graphics-launcher.sh" >/dev/null; then exit 1; fi
echo 'PASS running game rejected'
if TEST_RC=7 bash "$T/scripts/graphics-launcher.sh" >/dev/null; then exit 1; fi
[ ! -f "$SKYRIM_HOME/run/launch-pending" ]
echo 'PASS launcher failure propagated and marker cleared'
rm "$BASE_GAMEDIR/SkyrimSELauncher.exe"
if bash "$T/scripts/graphics-launcher.sh" >/dev/null; then exit 1; fi
echo 'PASS missing launcher reported without download'
