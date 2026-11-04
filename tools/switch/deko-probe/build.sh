#!/usr/bin/env bash
# Run inside devkitpro/devkita64 with the repository mounted as the working directory.
set -euo pipefail
probe_root="$(cd "$(dirname "$0")/../../.." && pwd)"
probe_out="$probe_root/build-switch-deko-probe"
mkdir -p "$probe_out/romfs/shaders"
/opt/devkitpro/tools/bin/uam -s vert -o "$probe_out/romfs/shaders/probe.vert.dksh" "$probe_root/tools/switch/deko-probe/probe.vert"
/opt/devkitpro/tools/bin/uam -s frag -o "$probe_out/romfs/shaders/probe.frag.dksh" "$probe_root/tools/switch/deko-probe/probe.frag"
/opt/devkitpro/tools/bin/uam -s vert -o "$probe_out/romfs/shaders/compose.vert.dksh" "$probe_root/tools/switch/deko-probe/compose.vert"
/opt/devkitpro/tools/bin/uam -s frag -o "$probe_out/romfs/shaders/compose.frag.dksh" "$probe_root/tools/switch/deko-probe/compose.frag"
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ \
    -std=c++17 -O2 -g -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft \
    -fPIE -I/opt/devkitpro/libnx/include -L/opt/devkitpro/libnx/lib \
    -specs=/opt/devkitpro/libnx/switch.specs \
    "$probe_root/tools/switch/deko-probe/main.cpp" \
    "$probe_root/tools/switch/deko-probe/graphics.cpp" -ldeko3dd -lnx -lm \
    -o "$probe_out/gt3r-deko-probe.elf"
/opt/devkitpro/tools/bin/nacptool --create "GT3 Deko3D Probe" "tomplayer07" "0.6.0-compose" "$probe_out/probe.nacp"
/opt/devkitpro/tools/bin/elf2nro "$probe_out/gt3r-deko-probe.elf" "$probe_out/gt3r-deko-probe.nro" \
    --nacp="$probe_out/probe.nacp" --icon="$probe_root/packaging/switch_icon.jpg" --romfsdir="$probe_out/romfs"
