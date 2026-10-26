# Menu Camera Logger (BakkesMod plugin)

Shows and logs which camera Rocket League's menus are looking through, so the
cameras in `MENU_Main_p.upk` can be given real names for Hebnix's Menu Map
camera tweak. It can also move the camera live as a preview.

## Use

- Loads automatically (`plugin load menucam` in `cfg/plugins.cfg`).
- An overlay in the top-left shows the camera's name (once you've given it
  one), focus actor, camera state, position, angle and FOV.
- Every camera switch is appended to `bakkesmod/data/menucam/log.txt`.
- Diagnostics are rewritten to `bakkesmod/data/menucam/preview-check.txt`
  every couple of seconds.
- F2 > Plugins > Menu Camera Logger:
  - **Camera offsets**: tabs for the home screen camera, the Play menu camera
    and the other fixed menu cameras. Each camera keeps its own move, tilt,
    turn and FOV, applied whenever that camera is on screen and saved in
    `data/menucam/cameras.txt`. Hebnix can't change the home screen camera,
    so its offsets only work with the plugin loaded.
  - **Car position**: move and turn the home screen cars live. Party cars turn
    around yours, and the home camera follows. These are the same numbers as
    Car position in Hebnix.
  - **Send to Hebnix**: saves the car position and the fixed cameras' offsets
    to `data/menucam/hebnix.json`. Hebnix's Menu Map page imports it with
    Import MenuCam values, and its Apply makes the change part of the menu.
- Console commands:
  - `menucam_name Home screen`: name the current camera (saved in
    `data/menucam/names.txt`). Run it with no name to clear the name.
  - `menucam_mark garage`: log the current camera with a note.
  - `menucam_car_live 1`, `menucam_car_x/y/z`, `menucam_car_yaw`: the car
    position values.
  - `menucam_export`: the same as Export for Hebnix.

## Build (Linux)

Needs `clang` (for `clang-cl`), `lld` and an xwin splat in `~/.cache/xwin/splat`:

```
cargo install --locked xwin
xwin --accept-license --cache-dir ~/.cache/xwin/cache splat --output ~/.cache/xwin/splat
./build.sh
```

The BakkesMod SDK comes from the Heroic prefix (override it with `BAKKES_SDK=`).
`fix_case.py` copies its headers and adds symlinks for the casing its
`#include`s use, since the SDK was written on Windows.
