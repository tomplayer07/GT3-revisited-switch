#!/usr/bin/env python3
"""Prepare an isolated SDL archive with a registered, externally supplied Deko driver.

Requires the exact SDL source/build behind --library. Does not change the source
checkout or installed archive. The archive includes GT3R_Deko_RenderDriver; the application supplies a native
backend through the versioned callback contract in backend.h.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--library', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
a = parser.parse_args()
source = a.source.resolve()
build = a.build.resolve()
library = a.library.resolve()
out = a.output.resolve()
if out == source or out == build or source in out.parents:
    parser.error('output must be separate from the SDL source/build')
original = subprocess.run(['aarch64-none-elf-ar', 'p', str(library), 'SDL_render.o'],
                          check=True, capture_output=True).stdout
built = build / 'CMakeFiles/SDL3-static.dir/src/render/SDL_render.o'
if original != built.read_bytes():
    parser.error('SDL_render.o differs: source/build do not match the supplied SDL archive')
text = (source / 'src/render/SDL_render.c').read_text()
marker = 'static const SDL_RenderDriver *render_drivers[] = {'
if text.count(marker) != 1:
    parser.error('SDL render-driver registration changed; review the integration')
text = text.replace(marker,
    '// Modified for GT3 Revisited: opt-in native Deko driver, provided by the app.\n'
    'extern SDL_RenderDriver GT3R_Deko_RenderDriver;\n' + marker +
    '\n    &GT3R_Deko_RenderDriver,', 1)
# SDL_render.c's source-relative internal includes must refer to the pinned source.
text = text.replace('#include "../', '#include "' + str(source / 'src') + '/')
out.mkdir(parents=True, exist_ok=True)
patched = out / 'SDL_render.c'
patched.write_text(text)
flags = {}
for line in (build / 'CMakeFiles/SDL3-static.dir/flags.make').read_text().splitlines():
    if ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
obj = out / 'SDL_render.o'
subprocess.run(['aarch64-none-elf-gcc', *flags['C_DEFINES'], *flags['C_INCLUDES'],
                '-I' + str(source / 'src/render'), *flags['C_FLAGS'],
                '-c', str(patched), '-o', str(obj)], check=True)
driver = Path(__file__).resolve().parents[3] / 'src/render/deko/SDL_render_deko.c'
driver_obj = out / 'SDL_render_deko.o'
subprocess.run(['aarch64-none-elf-gcc', *flags['C_DEFINES'], *flags['C_INCLUDES'],
                '-I' + str(Path(__file__).resolve().parents[3] / 'src'),
                *flags['C_FLAGS'], '-c', str(driver), '-o', str(driver_obj)], check=True)
archive = out / 'libSDL3-deko.a'
if archive == library:
    parser.error('output archive must not overwrite the input library')
shutil.copyfile(library, archive)
subprocess.run(['aarch64-none-elf-ar', 'r', str(archive), str(obj), str(driver_obj)], check=True)
# The input can contain the existing AUDOUT patch; every other object is retained.
manifest = {
    'source': str(source), 'build': str(build),
    'input_library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
    'original_render_object_sha256': hashlib.sha256(original).hexdigest(),
    'render_source_sha256': hashlib.sha256((source / 'src/render/SDL_render.c').read_bytes()).hexdigest(),
    'deko_driver_sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
    'deko_contract_sha256': hashlib.sha256((driver.parent / 'sdl_backend.h').read_bytes()).hexdigest(),
    'private_render_header_sha256': hashlib.sha256((source / 'src/render/SDL_sysrender.h').read_bytes()).hexdigest(),
    'output_library_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
    'status': 'SDL driver adapter built; application must provide GT3R_DekoBackend callbacks',
}
(out / 'sdl-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(archive)
