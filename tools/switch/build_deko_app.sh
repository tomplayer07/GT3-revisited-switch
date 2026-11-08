#!/usr/bin/env bash
# Same container/mounts as the probe. Reuse the existing engine build, compile
# the actual app as a separate target and never replace the GLES executable.
set -euo pipefail
source /opt/devkitpro/switchvars.sh
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
app_out="$project_root/build-switch-deko-app"
python3 "$project_root/tools/switch/deko-sdl/prepare_sdl.py" \
  --source /Desktop/SDL3-test --build /Desktop/SDL3-test/build-switch \
  --library "$project_root/build-switch/sdl-audio-fixed/libSDL3.a" --output "$app_out"
mkdir -p "$app_out/romfs/shaders"
# Compile from the container filesystem. Docker Desktop's shared mount can
# briefly expose updated bytes with a stale size to UAM's seek/tell file reader.
shader_stage_dir="$(mktemp -d)"
trap 'rm -rf "$shader_stage_dir"' EXIT
for stage in vert frag; do
  cp "$project_root/tools/switch/deko-probe/ui.$stage" "$shader_stage_dir/ui.$stage"
  cp "$project_root/tools/switch/deko-model/model.$stage" "$shader_stage_dir/model.$stage"
  /opt/devkitpro/tools/bin/uam -s "$stage" -o "$app_out/romfs/shaders/ui.$stage.dksh" "$shader_stage_dir/ui.$stage"
  /opt/devkitpro/tools/bin/uam -s "$stage" -o "$app_out/romfs/shaders/model.$stage.dksh" "$shader_stage_dir/model.$stage"
done
# One source, two shader variants. Insert after #version for GLSL preprocessing.
sed '1a#define GT3R_COMMON_FRAGMENT 1' "$shader_stage_dir/model.frag" > "$shader_stage_dir/common.frag"
/opt/devkitpro/tools/bin/uam -s frag -o "$app_out/romfs/shaders/common.frag.dksh" "$shader_stage_dir/common.frag"
cp "$project_root/tools/switch/deko-model/capture.frag" "$shader_stage_dir/capture.frag"
/opt/devkitpro/tools/bin/uam -s frag -o "$app_out/romfs/shaders/capture.frag.dksh" "$shader_stage_dir/capture.frag"

cmake -S "$project_root" -B "$project_root/build-switch" -DGT3R_BUILD_SWITCH_DEKO_APP=ON -DGT3R_DEKO_APP_DIR="$app_out"
cmake --build "$project_root/build-switch" --target gt3r_deko_app --parallel 6
