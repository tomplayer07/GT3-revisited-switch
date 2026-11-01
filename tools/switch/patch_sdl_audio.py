#!/usr/bin/env python3
"""Build the queued AUDOUT driver into a copy of an existing Switch SDL archive.

Run inside the devkitPro environment. The SDL source/build must match the archive.
Only the copy under --output is changed; the installed SDL remains intact.
"""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--library', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
# Refuse to mix a different installed driver with this source/build's private ABI.
original = subprocess.run(['aarch64-none-elf-ar', 'p', str(a.library), 'SDL_switchaudio.o'],
                          check=True, capture_output=True).stdout
built = a.build / 'CMakeFiles/SDL3-static.dir/src/audio/switch/SDL_switchaudio.o'
if original != built.read_bytes():
    p.error('SDL archive does not match the supplied SDL build')
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
for name in ('SDL_switchaudio.c', 'SDL_switchaudio.h'):
    dest = out / 'src/audio/switch' / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(a.source / 'src/audio/switch' / name, dest)
patch = Path(__file__).resolve().parents[2] / 'cmake/patches/sdl-switch-audio-queue.patch'
subprocess.run(['patch', '-p1', '-i', str(patch)], cwd=out, check=True)
# Use matching private SDL headers while compiling the two patched files.
for file in (out / 'src/audio/switch').iterdir():
    file.write_text(file.read_text().replace('"../SDL_sysaudio.h"', '"audio/SDL_sysaudio.h"'))
flags = {}
for line in (a.build / 'CMakeFiles/SDL3-static.dir/flags.make').read_text().splitlines():
    if ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
obj = out / 'SDL_switchaudio.o'
subprocess.run(['aarch64-none-elf-gcc', *flags['C_DEFINES'], *flags['C_INCLUDES'],
                *flags['C_FLAGS'], '-c', str(out / 'src/audio/switch/SDL_switchaudio.c'),
                '-o', str(obj)], check=True)
lib = out / 'libSDL3.a'
shutil.copyfile(a.library, lib)
subprocess.run(['aarch64-none-elf-ar', 'r', str(lib), str(obj)], check=True)
print(f'Patched SDL archive: {lib}')
