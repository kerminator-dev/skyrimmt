#!/bin/bash
# Open Bethesda's installed launcher in the same environment/profile as Play.
. "$(dirname "$0")/_env.sh"
. "$(dirname "$0")/ini.sh"
game_running && { echo "Quit Skyrim before changing graphics settings."; exit 1; }
loader_running && { echo "A Skyrim launcher is already open."; exit 1; }
[ -d "$SKYRIM_HOME/run/deploy.lock" ] && { echo "Wait for mod deployment to finish."; exit 1; }
exe="$GAMEDIR/SkyrimSELauncher.exe"
[ -f "$exe" ] || exe="$BASE_GAMEDIR/SkyrimSELauncher.exe"
[ -f "$exe" ] || { echo "SkyrimSELauncher.exe is missing from the installed game."; exit 1; }
mkdir -p "$SKYRIM_HOME/run" "$SKYRIM_HOME/backups" "$SKYRIM_HOME/config" || exit 1
backup=$(mktemp -d "$SKYRIM_HOME/backups/before-graphics-launcher.XXXXXX") || exit 1
for ini in Skyrim.ini Skyrim.INI SkyrimPrefs.ini SkyrimCustom.ini; do
  [ ! -f "$MYGAMES_DIR/$ini" ] || cp -p "$MYGAMES_DIR/$ini" "$backup/" || exit 1
done
prefs="$MYGAMES_DIR/SkyrimPrefs.ini"
before="$(ini_get "$prefs" Display 'iSize W')x$(ini_get "$prefs" Display 'iSize H')"
echo $$ > "$SKYRIM_HOME/run/launch-pending"
trap 'rm -f "$SKYRIM_HOME/run/launch-pending"' EXIT
ensure_steam || { echo "Steam is not ready. Open Steam and sign in or choose Offline Mode."; exit 1; }
"$SKYRIMMT_ROOT/scripts/run-exe.sh" --wait "$exe"
rc=$?
if [ "$rc" = 0 ]; then
  after="$(ini_get "$prefs" Display 'iSize W')x$(ini_get "$prefs" Display 'iSize H')"
  # Only a changed, valid resolution takes ownership away from Fit/percentage.
  # Cancelling the original launcher leaves the user's display selection alone.
  if [ "$after" != "$before" ] && [[ "$after" =~ ^[1-9][0-9]*x[1-9][0-9]*$ ]]; then
    printf 'original\n' > "$SKYRIM_HOME/config/window-size"
  fi
  echo "Graphics settings saved. Start the game with Play in SkyrimMT."
fi
exit "$rc"
