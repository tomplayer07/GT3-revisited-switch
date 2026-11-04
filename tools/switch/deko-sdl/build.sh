#!/usr/bin/env bash
# Mount the repository, matching SDL source and portlibs at their original build paths.
set -euo pipefail
source /opt/devkitpro/switchvars.sh
probe_root="$(cd "$(dirname "$0")/../../.." && pwd)"
probe_out="$probe_root/build-switch-deko-sdl"
python3 "$probe_root/tools/switch/deko-sdl/prepare_sdl.py" \
    --source /Desktop/SDL3-test --build /Desktop/SDL3-test/build-switch \
    --library "$probe_root/build-switch/sdl-audio-fixed/libSDL3.a" --output "$probe_out"
mkdir -p "$probe_out/romfs/shaders"
for shader in probe compose ui; do
    for stage in vert frag; do
        /opt/devkitpro/tools/bin/uam -s "$stage" \
            -o "$probe_out/romfs/shaders/$shader.$stage.dksh" \
            "$probe_root/tools/switch/deko-probe/$shader.$stage"
    done
done
for stage in vert frag; do
    /opt/devkitpro/tools/bin/uam -s "$stage" -o "$probe_out/romfs/shaders/model.$stage.dksh" \
        "$probe_root/tools/switch/deko-model/model.$stage"
done
/opt/devkitpro/tools/bin/uam -s frag -o "$probe_out/romfs/shaders/capture.frag.dksh" \
    "$probe_root/tools/switch/deko-model/capture.frag"
# Reuse current engine objects for the actual race camera, without rebuilding the app NRO.
cmake --build "$probe_root/build-switch" --target gt3r_engine --parallel 6
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ \
    -std=c++20 -O2 -g -Wall -Wextra -DGT3R_DEKO_SDL -DGT3R_DEKO_MODEL -DSDL_MAIN_HANDLED \
    -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE \
    -I"$probe_root/src" -isystem /Desktop/switch-portlibs/include -isystem /opt/devkitpro/libnx/include \
    -L/opt/devkitpro/libnx/lib -L/opt/devkitpro/portlibs/switch/lib \
    -specs=/opt/devkitpro/libnx/switch.specs \
    "$probe_root/tools/switch/deko-sdl/main.cpp" "$probe_root/tools/switch/deko-probe/graphics.cpp" \
    "$probe_root/tools/switch/deko-model/model.cpp" "$probe_root/tools/switch/deko-session/session.cpp" "$probe_root/src/render/math3d.cpp" \
    -Wl,--start-group "$probe_out/libSDL3-deko.a" "$probe_root/build-switch/libgt3r_engine.a" /opt/devkitpro/portlibs/switch/lib/libz.a \
    "$probe_root/build-switch/_deps/libchdr-build/libchdr-static.a" \
    "$probe_root/build-switch/_deps/libchdr-build/deps/lzma-25.01/libchdr-lzma.a" \
    "$probe_root/build-switch/_deps/libchdr-build/deps/miniz-3.1.1/libminiz.a" \
    "$probe_root/build-switch/_deps/libchdr-build/deps/zstd-1.5.7/libzstd.a" \
    -ldeko3dd -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx -lm -Wl,--end-group \
    -o "$probe_out/gt3r-deko-sdl-probe.elf"
/opt/devkitpro/tools/bin/nacptool --create "GT3 SDL Deko3D Probe" "tomplayer07" "0.6.0-opponents" "$probe_out/probe.nacp"
/opt/devkitpro/tools/bin/elf2nro "$probe_out/gt3r-deko-sdl-probe.elf" "$probe_out/gt3r-deko-sdl-probe.nro" \
    --nacp="$probe_out/probe.nacp" --icon="$probe_root/packaging/switch_icon.jpg" --romfsdir="$probe_out/romfs"
