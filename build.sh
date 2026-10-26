#!/usr/bin/env bash
# Builds MenuCam.dll on Linux with clang-cl + lld-link against an xwin splat
# of the MSVC CRT and Windows SDK.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
xwin="${XWIN:-$HOME/.cache/xwin/splat}"
sdk="${BAKKES_SDK:-$(echo "$HOME"/Games/Heroic/Prefixes/rocketleague/pfx/drive_c/users/*/AppData/Roaming/bakkesmod/bakkesmod/bakkesmodsdk)}"
out="$here/build"
mkdir -p "$out/obj"

if [ ! -d "$out/sdk-include" ]; then
    python3 -I "$here/fix_case.py" "$sdk/include" "$out/sdk-include"
fi
if [ ! -f "$out/imgui/imgui.h" ]; then
    unzip -q -o "$sdk/imgui_bm.zip" -d "$out"
fi

flags=(
    --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /MD /O2 /DNDEBUG
    /DWIN32 /D_WINDOWS /D_USRDLL /DNOMINMAX
    -fms-compatibility-version=19.40
    -Wno-everything
    /imsvc "$xwin/crt/include"
    /imsvc "$xwin/sdk/include/ucrt"
    /imsvc "$xwin/sdk/include/um"
    /imsvc "$xwin/sdk/include/shared"
    /I "$out/sdk-include" /I "$out"
)

objs=()
for src in "$here/src/MenuCam.cpp" "$out/imgui/imgui.cpp" "$out/imgui/imgui_draw.cpp" "$out/imgui/imgui_widgets.cpp"; do
    obj="$out/obj/$(basename "${src%.cpp}").obj"
    clang-cl "${flags[@]}" /c "$src" "/Fo$obj"
    objs+=("$obj")
done

lld-link /nologo /DLL /OUT:"$out/MenuCam.dll" "${objs[@]}" \
    "$sdk/lib/pluginsdk.lib" \
    /LIBPATH:"$xwin/crt/lib/x86_64" \
    /LIBPATH:"$xwin/sdk/lib/um/x86_64" \
    /LIBPATH:"$xwin/sdk/lib/ucrt/x86_64"

echo "Built $out/MenuCam.dll"
