# Menu Camera Logger (BakkesMod plugin)

Shows and logs which camera Rocket League's menus are looking through, so the
cameras in `MENU_Main_p.upk` can be given real names for Hebnix's Menu Map
camera tweak. It can also move the camera live as a preview.

## Use

- Loads automatically (`plugin load menucam` in `cfg/plugins.cfg`).
- An overlay in the top-left shows the camera's name (once you've given it
  one), focus actor, camera state, position, angle and FOV.
- Diagnostics for the live previews are rewritten to
  `bakkesmod/data/menucam/preview-check.txt` every couple of seconds.
- Every camera switch is appended to `bakkesmod/data/menucam/log.txt`.
- F2 > Plugins > Menu Camera Logger: name the current camera, log it, and turn
  on the live offset sliders.
- Console commands:
  - `menucam_name Home screen`: name the current camera (saved in
    `data/menucam/names.txt`). Run it with no name to clear the name.
  - `menucam_mark garage`: log the current camera with a note.
  - `menucam_hooks`: print which camera hooks fire and whether the live preview
    stays in place (the same numbers as the Preview check panel).
  - `menucam_live 1`, `menucam_x/y/z`, `menucam_pitch/yaw`, `menucam_fov`: the
    live camera preview offsets. `menucam_method` picks how they're applied:
    `0` (default) uses the camera's shake offsets, `1` overwrites the view.
  - `menucam_car_live 1`, `menucam_car_x/y/z`, `menucam_car_yaw`: move the
    home screen cars live. Party cars turn around yours, and the home camera
    follows. These are the same numbers as Car position in Hebnix.

  None of the live values are saved to the game. Hebnix's Menu Map page makes
  the change permanent.

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
