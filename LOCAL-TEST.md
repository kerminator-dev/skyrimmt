# Local offline and display build

Current source includes upstream v1.0.7 (d0b3666583a5f3f0e882cecb0e04892ed01eab0d).
The Mac Air M4 preset has been removed; the independent FPS controls remain.
The installed-app paths and gameplay validation below describe the earlier v1.0.6 local build.

## v1.0.7 source validation

- Full Swift app typecheck passed (existing deprecated LaunchServices API warning).
- Offline Steam: 12 checks passed; graphics launcher: 5 checks passed.
- Session management: 25 checks passed; Steam language sync: 33 checks passed.
- Preset fixture: `mac` rejected; standard preset applies with backups and preserves
  resolution, controller preference and the independent FPS cap.
- The v1.0.7 source has not been rebuilt into an installed app or tested in gameplay.

Test app: `/Users/nikita/Applications/SkyrimMT Test.app` (v1.0.6-local.8).
Original app remains at `/Users/nikita/Downloads/SkyrimMT.app`.
Both use the existing `/Users/nikita/Games/skyrim` environment. Do not run them
simultaneously. There is no need to run Setup or download Skyrim again.

## Test

1. Open the test app. Steam is already configured for Offline Mode in this installation.
2. Press Play. Steam library readiness should be accepted offline; status should
   show `Offline — ready` rather than waiting for an online connection.
3. Game settings: enable `Stretch to full screen` and select a scale from 25% to 100% in 5% steps. Each option shows its pixel dimensions.
   The user's current resolution selection is preserved by the update.
4. Check a 3D scene as well as the HUD. The local.3 fix keeps GetClientRect
   logical dimensions in Skyrim's main EXE while Wine/DXMT see the physical
   full-screen window. This corrects the local.1 quarter-scene crop at 50%.
5. `Game → Advanced graphics → Open Skyrim launcher…` opens the installed
   original launcher. Choose Options → Advanced, save with OK and close it.
   Start the game with SkyrimMT's Play button to retain its Wine/SKSE setup.
   A changed resolution selects `Resolution from Skyrim launcher` automatically;
   cancelling without a resolution change preserves the previous selection.
   INIs are backed up under `backups/before-graphics-launcher.*` before opening.

The display proxy is for DXMT. It preserves the render buffer during window
resizing, does not change the macOS display resolution, and requires no SKSE.
An unrelated custom d3d11.dll is never overwritten: the launcher reports the
conflict and asks for the feature/frame recording to be disabled.

## Validation performed

- Native app compiled and final app passed `codesign --verify --deep --strict`.
- `bash tests/session/offline.sh`: 12 checks passed (including stale logs,
  restarted sessions, offline library readiness, and GOG).
- `bash tests/session/run.sh`: 25 checks passed.
- `bash tests/session/graphics-launcher.sh`: cancellation, changed resolution,
  INI backup, running-game rejection, failed launch and missing-executable checks passed.
- The GPU gradient test in `tests/session/display.c` reproduced the crop with
  the old proxy at 50% (corner pixel 115,115 instead of approximately 230,230).
  It passes with the new proxy at 50%, 75%, 100%, and with stretching disabled.
  It also verifies physical window size, game client size, and ResizeBuffers.
- Actual Skyrim launch with the new proxy: Steam ready offline; game running;
  `game viewport fixed at 960x620` and `render 960x620 -> window 1920x1243`.
- The installed original SkyrimSELauncher.exe was started in the same Wine prefix.
- Native UI capture remains intermittent. A full gameplay/mouse test is still
  needed; the automated tests do not load or modify saves.

## Rollback

Quit the test app/game and open the original app. The original launch script
removes the marked display proxy on the next DXMT launch. To return the old
resolution, choose `Fit screen` in Game settings. Original window-size and
SkyrimPrefs.ini were also backed up in
`/Users/nikita/Games/skyrim/backups/local-offline-display-20261117`.

## Build notes

Source base: v1.0.7 (d0b3666583a5f3f0e882cecb0e04892ed01eab0d).
Build with mingw-w64 on PATH and:
`SKYRIMMT_VERSION=v1.0.7-local.1 bash app/build.sh --no-install`.
The locally installed test app retains optional LOOT/benchmark helpers and the
keychain helper from the user's original release; nothing was published.
