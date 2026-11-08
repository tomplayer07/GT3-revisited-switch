#!/usr/bin/env bash
# Mount the repository, matching SDL source and portlibs at their original build paths.
set -euo pipefail
source /opt/devkitpro/switchvars.sh
probe_root="$(cd "$(dirname "$0")/../../.." && pwd)"
case "${GT3R_DEKO_VARIANT:-race}" in
    race)
        probe_out="$probe_root/build-switch-deko-sdl"
        probe_main="$probe_root/tools/switch/deko-sdl/main.cpp"
        probe_name="gt3r-deko-sdl-probe"
        probe_title="GT3 SDL Deko3D Probe"
        ;;
    frontend)
        probe_out="$probe_root/build-switch-deko-frontend"
        probe_main="$probe_root/tools/switch/deko-sdl/frontend_main.cpp"
        probe_name="gt3r-deko-frontend"
        probe_title="GT3 Deko3D Frontend"
        ;;
    *) echo "GT3R_DEKO_VARIANT must be race or frontend" >&2; exit 2 ;;
esac
python3 "$probe_root/tools/switch/deko-sdl/prepare_sdl.py" \
    --source /Desktop/SDL3-test --build /Desktop/SDL3-test/build-switch \
    --library "$probe_root/build-switch/sdl-audio-fixed/libSDL3.a" --output "$probe_out"
mkdir -p "$probe_out/romfs/shaders"
# UAM must read a stable local file, not a concurrently refreshed Docker mount.
shader_stage_dir="$(mktemp -d)"
trap 'rm -rf "$shader_stage_dir"' EXIT
for shader in probe compose ui; do
    for stage in vert frag; do
        cp "$probe_root/tools/switch/deko-probe/$shader.$stage" "$shader_stage_dir/$shader.$stage"
        /opt/devkitpro/tools/bin/uam -s "$stage" \
            -o "$probe_out/romfs/shaders/$shader.$stage.dksh" \
            "$shader_stage_dir/$shader.$stage"
    done
done
for stage in vert frag; do
    cp "$probe_root/tools/switch/deko-model/model.$stage" "$shader_stage_dir/model.$stage"
    /opt/devkitpro/tools/bin/uam -s "$stage" -o "$probe_out/romfs/shaders/model.$stage.dksh" \
        "$shader_stage_dir/model.$stage"
done
cp "$probe_root/tools/switch/deko-model/capture.frag" "$shader_stage_dir/capture.frag"
sed '1a#define GT3R_COMMON_FRAGMENT 1' "$shader_stage_dir/model.frag" > "$shader_stage_dir/common.frag"
/opt/devkitpro/tools/bin/uam -s frag -o "$probe_out/romfs/shaders/common.frag.dksh" "$shader_stage_dir/common.frag"
/opt/devkitpro/tools/bin/uam -s frag -o "$probe_out/romfs/shaders/capture.frag.dksh" \
    "$shader_stage_dir/capture.frag"
# Reuse current engine objects for the actual race camera, without rebuilding the app NRO.
cmake --build "$probe_root/build-switch" --target gt3r_engine --parallel 6
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ \
    -std=c++20 -O2 -g -Wall -Wextra -D__SWITCH__ -DGT3R_DEKO_SDL -DGT3R_DEKO_MODEL -DSDL_MAIN_HANDLED \
    -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE \
    -I"$probe_root/src" -isystem /Desktop/switch-portlibs/include -isystem /opt/devkitpro/portlibs/switch/include -isystem /opt/devkitpro/libnx/include \
    -L/opt/devkitpro/libnx/lib -L/opt/devkitpro/portlibs/switch/lib \
    -specs=/opt/devkitpro/libnx/switch.specs \
    "$probe_main" "$probe_root/tools/switch/deko-probe/graphics.cpp" \
    "$probe_root/tools/switch/deko-model/model.cpp" "$probe_root/tools/switch/deko-session/session.cpp" "$probe_root/src/render/math3d.cpp" \
    -Wl,--start-group "$probe_out/libSDL3-deko.a" "$probe_root/build-switch/libgt3r_engine.a" /opt/devkitpro/portlibs/switch/lib/libz.a \
    "$probe_root/build-switch/_deps/libchdr-build/libchdr-static.a" \
    "$probe_root/build-switch/_deps/libchdr-build/deps/lzma-25.01/libchdr-lzma.a" \
    "$probe_root/build-switch/_deps/libchdr-build/deps/miniz-3.1.1/libminiz.a" \
    "$probe_root/build-switch/_deps/libchdr-build/deps/zstd-1.5.7/libzstd.a" \
    -ldeko3dd -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx -lm -Wl,--end-group \
    -o "$probe_out/$probe_name.elf"
/opt/devkitpro/tools/bin/nacptool --create "$probe_title" "tomplayer07" "0.6.1-dk-compare" "$probe_out/probe.nacp"
/opt/devkitpro/tools/bin/elf2nro "$probe_out/$probe_name.elf" "$probe_out/$probe_name.nro" \
    --nacp="$probe_out/probe.nacp" --icon="$probe_root/packaging/switch_icon.jpg" --romfsdir="$probe_out/romfs"
