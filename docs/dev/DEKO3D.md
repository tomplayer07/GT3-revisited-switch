# Deko3D implementation plan — Nintendo Switch

Status: standalone native presentation, indexed reversed depth, texture upload
and offscreen composition confirmed on hardware, 2026-11-02.
Branch: `experiment-deko3d`, based on Switch 0.6.0 (`44aca31`).
The standalone native prototype is implemented; the game backend is not yet ported.

## Objective and decision

Build an optional native Deko3D backend and measure whether reducing graphics
submission overhead improves night races while preserving the released Switch
renderer's image. A successful triangle demo proves integration, not game speed.

Recommended architecture: a shared Deko3D device/queue/resource layer, used by
both a native GT3 3D backend and an SDL3 **2D render driver** maintained as a
versioned patch to the Switch SDL source. This keeps menu/HUD callers using
SDL_Renderer while all graphics in a Deko3D build belong to the same device.
This is a proposed implementation, not an existing SDL capability.

The previous plan assumed an offscreen Deko3D texture could be presented through
the existing GLES UI. That interoperability has not been established. Do not base
the port on importing Deko3D images into EGL or running two independent owners of
the default Switch window. The early 2D/3D composition milestone below must prove
resource sharing and command ordering before porting the race renderer.

A full SDL_GPU Deko3D driver remains an alternative if the native approach proves
less maintainable. It would reuse more of the desktop renderer but require a
larger SDL GPU API surface and a shader-format integration. Decide against the
measured scope at the first milestone; do not build both approaches in parallel.

## Inspected baseline

- `src/app/app.cpp` creates a Switch OpenGL window and presents through
  `SDL_RenderPresent`. Window creation/presentation must become backend-specific.
- `GpuContext`, `Uploader`, `GpuModel`, and `View3D` expose SDL_GPU types. Their
  Switch branches also use native GL objects, sometimes cast into SDL handles.
  An SDL_GPU type in a signature does not imply that the native Switch renderer
  is an SDL_GPU driver. Deko3D needs explicit resource ownership and routing.
- `View3D::draw`, offscreen views and cube rendering issue GL commands directly.
  The scene texture is wrapped as an SDL GLES texture and composited by SDL.
- `src/app/ui*`, `src/render/screen_batch.cpp`, menus and fonts use SDL rendering
  directly. Movies upload streaming IYUV textures. Porting only `View3D` leaves
  these substantial dependencies unresolved.
- The local SDL source inspected at `/Users/lorenzo/Desktop/SDL3-test` has an
  EGL Switch video path using `nwindowGetDefault()`. No Deko3D render driver was
  found there. Confirm this source revision matches the installed archive before
  creating patches; the audio patch script already requires that ABI match.

The inspected `devkitpro/devkita64:latest` container contains:

| Component | Installed version |
| --- | --- |
| deko3d | 0.5.0-1 |
| uam | 1.1.0-1 |
| libnx | 4.12.0-1 |
| devkitA64 | r29.2-1 |

Headers are under `/opt/devkitpro/libnx/include`; release/debug libraries are
`libdeko3d.a` and `libdeko3dd.a`; `uam` is under `/opt/devkitpro/tools/bin`.
Use `dkp-pacman` to query packages in this image. Pin the image digest, package
versions and SDL source revision for the experiment; `latest` is not a lockfile.
Availability is confirmed; compatibility and hardware operation are not yet tested.

## Backend contract

Planned module boundaries (names below are proposals):

| Area | Work |
| --- | --- |
| `src/render/deko/` | device, queues, memory pools, resource wrappers, command recording, pipeline state and shader loading |
| `cmake/patches/` and `tools/switch/` | reproducible SDL 2D driver/video patches and shader/build tooling |
| `GpuContext` / `Uploader` | backend selection, typed buffer/image handles, uploads and retirement |
| `GpuModel` | geometry buffers, textures, dynamic updates and resource ownership |
| `View3D` / screen batches | opaque/blended passes, offscreen targets, compositing, mirrors and reflection maps |
| `src/app/app.cpp` | startup, window flags, renderer selection and one presentation owner |

Use a narrow C interface between the SDL driver and shared Deko implementation;
keep Deko objects inside that implementation. Establish who owns each object:

- Context owns device, graphics queue, descriptor storage and swapchain.
- Frame slots own command memory, transient uniforms/uploads and completion fences.
- Images/buffers own allocations; borrowed SDL texture wrappers never free them.
- Destruction and resizing retire resources only after GPU completion.
- SDL's 2D queue is flushed before native 3D work; native work is recorded before
  SDL samples its output. One shared queue provides ordering, with the required
  GPU barriers. `SDL_RenderPresent` submits/presents the final composed image.

Start with a small bounded number of frames in flight (two initially). Grow
memory pools deliberately and log their high-water marks. Avoid per-draw heap
allocations, CPU waits per pass, or a queue-idle wait every frame. CPU readback is
reserved for requested screenshots/debugging, never normal presentation.

## Ordered milestones and exit checks

### 0. Reproducible build and SDL ownership audit

Add an opt-in build selection such as `GT3R_SWITCH_RENDERER=gles|deko3d`, with
GLES as default, and separate `build-switch-deko` output. Produce a distinctly
named `gt3r-deko.nro` with an experimental version suffix. Do not replace the
release Desktop/Switch artifact during early development.

Locate and pin the SDL source/build matching the archive. Integrate the queued
AUDOUT correction into the experimental SDL build. Inventory the render-driver
callbacks and texture formats actually used, including render targets, clipping,
logical scaling, geometry, custom blend modes, rotated copies and IYUV movies.
Define native-window acquisition and teardown without creating an EGL surface
in the Deko build. Record both debug and release linking paths.

Exit: documented, reproducible dependencies and an agreed SDL/device ownership
contract. Missing Deko dependencies fail configuration clearly. GLES continues to
configure independently. This milestone resolves the largest architectural risk.

### 1. Native device, shaders and presentation

Create a separate small Switch executable using the planned core. Compile vertex
and fragment GLSL with `uam` to DKSH; load an indexed textured primitive, create
colour/depth targets, record commands and acquire/present swapchain images.
Exercise repeated frames, input-driven exit and resource teardown on hardware.

Use explicit shader bindings and uniform layouts. Validate reversed depth
(clear 0, greater-or-equal), clip-space conversion, winding, image origin and
RGBA ordering with asymmetric geometry and textures. Do not blindly reuse the
GLES Y flip or depth remap.

Exit: stable native presentation and correct textures/depth over repeated runs.
No gameplay performance claim is made from this demo.

### 2. SDL 2D and native 3D composition — critical gate

Implement the SDL 2D driver on the shared core: textured geometry, clipping,
viewport/logical scaling, blend state, texture updates and render targets. Add a
controlled way to wrap a borrowed Deko render-target image. Demonstrate the
sequence UI background → native offscreen 3D → texture composite → UI foreground
in one frame, with no CPU readback or GL texture import.

Exercise fonts, transparent sprites, nested render targets, resize/recreation,
colour/alpha modulation, rotated/flipped copies and the movie texture path.
Include screenshot readback on demand. Preserve the audio fix in the same SDL
archive; record patches and source hashes rather than editing installed portlibs.

Exit: menu/HUD-like composition is correct, resources have a single owner, and
memory remains bounded through repeated create/destroy cycles. If this gate
requires a costly per-frame copy or incompatible ownership, revisit architecture
before investing in the race renderer.

### 3. Model and material path

Introduce typed resources incrementally at the existing renderer boundary;
keep the GLES implementation available. Port buffer uploads, texture format
conversion/layout, mipmaps, samplers, uniform binding and pipeline/state caching.
Use the shared canonical GLSL as the source for Deko variants; derive binding
and coordinate adaptations in tooling and track uniform layout compatibility.

Bring up one opaque model, then static course scenery and the actual car
materials. Match alpha test, blending, masks, fog, depth bias/write policy and
vertex colours. Port dynamic morph/wheel geometry and palette/texture updates
with fenced upload rings and invalidation rules.

Exit: representative day/night models render correctly, including Mazda 787B
lettering, wheels, opaque body depth and metallic prize cups. An opaque-only
scene may locate overhead, but cannot decide whether the full game is faster.

### 4. Race feature parity with the released Switch build

Port environment cubes, sphere maps, mirrors, screen overlays, sun/lamp glows,
car-light occlusion, projected shadows, and the existing Switch wet-road path.
Retain current CPU-side visibility optimizations during A/B comparisons. Do not
restore effects deliberately absent from the released Switch build during this
comparison, since that would change the workload.

Check arcade night/day races, GT Mode licences and prize screens, replays/demo,
pausing, save/load transitions, movies and split screen. Recheck audio during
heavy rendering. Loading/unloading courses must reclaim GPU resources safely.

Exit: gameplay is visually comparable and stable enough for a timed benchmark;
remaining unsupported optional features are explicitly listed and disabled.

### 5. Controlled performance comparison and decision

Use the released `44aca31` GLES renderer as the baseline. Prefer the same saved
replay or scripted camera over manually repeated laps. Fix track, opponents,
resolution, render scale, clocks, handheld/docked mode and all effect settings.
Use application mode with the same memory availability. Warm up resources, then
run at least three paired 90-second measurements, alternating backend order.

Measure CPU simulation, scene preparation, command recording/submission, GPU
render time, fence/acquire/present waits, memory/upload volume, and median/p95/p99
frame times. Mark loading/pause windows separately. Keep diagnostic writes
deferred in both builds; confirm the conclusion with profiling disabled.
GPU timings should be collected asynchronously where supported; if unavailable,
report that limitation rather than interpreting CPU submission as GPU time.
CPU and GPU overlap, so their times cannot simply be added or subtracted.

Proposed promotion target: at least 10% lower median frame time in reproducible
night-track workloads that are not already capped at 60 FPS, improvement in the
slow-frame tail, and no material regression in day scenes or visual correctness.
This is a decision threshold, not a promised result. If gains are within run
variance, keep GLES as the release backend and use the data to target CPU work.

## Risks and delivery scope

The main risks are the SDL 2D integration, explicit synchronization/resource
lifetimes, shader/material parity and maintaining the SDL patch across updates.
The first deliverable is milestones 0–2: a hardware prototype proving the
complete presentation path. It will not yet run a race. A complete port follows
only after this gate; estimates for it should be revised using the prototype's
actual implementation effort. This is a multi-stage renderer project, not an
API-name replacement, and it does not promise 60 FPS everywhere.

## References

- [Deko3D project](https://github.com/devkitPro/deko3d)
- [Deko3D primer: memory, queues, images and synchronization](https://github.com/devkitPro/deko3d/blob/master/Primer.md)
- [UAM shader compiler](https://github.com/devkitPro/uam)
- Local engine overview: [RENDERING.md](RENDERING.md)
- Existing audio patch: `cmake/patches/sdl-switch-audio-queue.patch`

## First implemented deliverable

`tools/switch/deko-probe/` now contains an independent native presentation probe.
It creates a Deko device, two 720p swapchain images, immutable command lists,
and shaders compiled by UAM and embedded in RomFS. No SDL/EGL context is created.
The image is an asymmetric RGB triangle on a dark background; press Plus to exit.
Resource cleanup waits for the GPU once, at shutdown, before freeing resources.

Build from the repository root:

```sh
docker --context default run --rm \
  -v "$PWD:/work" -w /work devkitpro/devkita64:latest \
  bash tools/switch/deko-probe/build.sh
```

Output: `build-switch-deko-probe/gt3r-deko-probe.nro`.
The first RGB-triangle probe compiled and ran on the console, and exited with
Plus successfully (user confirmed). The current second probe adds an actual
16-bit index buffer and a Z24X8 depth attachment per swapchain image, using
reversed depth (clear 0, greater-or-equal) as the game does. It draws the near RGB
triangle first and the far, larger yellow triangle second; the RGB triangle must
remain visible in their overlap. The depth/index revision was also confirmed on hardware by screenshot: the
near RGB triangle remains visible over the yellow triangle drawn after it.

The current third revision adds a real 64x64 RGBA checkerboard uploaded from
a linear staging buffer into a GPU block-linear image, with image/sampler
descriptors and nearest filtering. Upload is submitted once and flushed before
the draw batch; no per-frame CPU upload or readback is used. The depth test
remains on the left; the texture is on a separate quad on the right. Expected
corner markers: top-left red, top-right green, bottom-left blue, bottom-right
yellow. After correcting the transfer strides, this revision was confirmed on hardware:
the screenshot shows correct depth, checkerboard and red/green/blue/yellow corner
orientation. The diagnostic log reaches `shutdown complete` without Deko errors.
SDL composition remains a subsequent milestone. Deko API usage
was checked against the switchbrew `graphics/deko3d/deko_basic` example.

### Texture prototype correction

The first texture revision crashed on hardware before showing a frame. Its
`DkCopyBuf` incorrectly specified a 64-byte row pitch for 64 RGBA8 pixels
(256 bytes required), and a pixel count as the layer stride. The corrected
revision uses zero strides to request tightly packed data, as implemented by
Deko3D's `dkCmdBufCopyBufferToImage`. Hardware operation and clean shutdown
were subsequently confirmed by screenshot and diagnostic log.
It also installs the Deko debug callback and records startup stages in
`sdmc:/switch/gt3r-deko-probe.log`; fatal callbacks flush the message and exit
without unwinding through invalid GPU state. No diagnostic SD writes occur in
the normal frame loop after the first presented frame.

### Native offscreen composition revision

The next revision renders the existing indexed/depth/texture scene into a
separate RGBA render target per swapchain image. A GPU fragment barrier and
texture-cache invalidation precede sampling that target in a full-scene quad.
The final image has a purple border and a cyan 50% alpha foreground panel.
A toggles the panel; Plus exits. The foreground pass disables depth testing and
uses straight-alpha colour blending with coverage accumulation for alpha.
There is no CPU image readback, per-frame resource upload, or CPU pass wait.
All command lists are immutable; resources survive until the queue is idle at
shutdown. Compiled successfully and confirmed on hardware: both screenshots
show correct offscreen orientation and depth, the purple border and correct cyan
alpha blending when enabled. Repeated A toggles are recorded in the log, which
ends with `shutdown complete` without Deko warnings/errors.

This confirms native GPU composition for the tested scene. It is not an SDL 2D
driver: the SDL menu/HUD integration gate remains open. Next work is to extract
the shared device/resource/command core from the standalone probe, then implement
a minimal SDL render driver against the exact installed SDL private ABI. Its
first hardware milestone must exercise SDL_RenderGeometry and texture updates,
compose a borrowed native render target, and preserve native frame ordering.

### Graphics ownership boundary extracted

The probe's native device, allocations, descriptors and command lists now live in
`tools/switch/deko-probe/graphics.cpp`. `graphics.h` exposes an owning PIMPL
interface; `main.cpp` handles RomFS, controller input and application lifetime.
The separate build compiles both translation units successfully. The rendered
scene and submission sequence are unchanged. This is preparatory refactoring;
the implementation still records the fixed probe scene and does not yet expose
resources or command recording to an SDL driver.

### SDL 2D driver: first executable integration

`tools/switch/deko-sdl/` now builds `gt3r-deko-sdl-probe.nro` against an isolated
copy of the exact SDL archive used by the port. `prepare_sdl.py` first compares
`SDL_render.o` in the input archive with the matching SDL build, then recompiles
SDL's renderer registry plus `SDL_render_deko.c`. All other objects, including
the queued AUDOUT driver, are retained. A manifest records source/header and
archive hashes. Installed SDL and its source checkout are not edited.

The driver accepts a versioned C callback table rather than exposing Deko types
inside SDL. It queues solid geometry and points, preserves viewport/clip/blend
state, and dispatches through the shared native device. Unsupported texture,
texture-update and render-target requests return explicit errors in the current
native implementation; arbitrary SDL textures, atlas fonts and movies are not
yet integrated. The driver adapter already has hooks for these later callbacks.

The SDL harness creates a non-OpenGL window, selects `gt3r-deko` explicitly, and
uses SDL_RenderFillRect, SDL_RenderPoints and SDL_RenderGeometry for its cyan
panel, bitmap SDL lettering and moving translucent triangle. SDL_RenderPresent
uses the same device/queue that renders the native offscreen scene. A toggles
the SDL overlay; Plus exits. The bitmap lettering deliberately uses points:
SDL_RenderDebugText in this SDL version requires an atlas texture, still pending.

Dynamic UI commands and uniforms use separate pools per swapchain slot. A GPU
fence is waited before CPU command-memory reuse; queue ordering preserves native
scene → sampled composition → SDL geometry. No normal-frame readback is used.
The new SDL renderer is released before the native graphics context, including
on error paths. Both the isolated SDL archive and full NRO compile successfully;
this SDL integration revision awaits hardware confirmation.

Build with read-only mounts for the matching SDL checkout and installed portlibs:

```sh
docker --context default run --rm \
  -v "$PWD:/Desktop/GT3-revisited" \
  -v /Users/lorenzo/Desktop/SDL3-test:/Desktop/SDL3-test:ro \
  -v /Users/lorenzo/Desktop/switch-portlibs:/Desktop/switch-portlibs:ro \
  -w /Desktop/GT3-revisited devkitpro/devkita64:latest \
  bash tools/switch/deko-sdl/build.sh
```

Output: `build-switch-deko-sdl/gt3r-deko-sdl-probe.nro`.
Next gate: hardware confirmation of the SDL-driven geometry, then native SDL
texture allocation/update/sampling and borrowed offscreen-image composition.

SDL geometry hardware follow-up: the first integration's screenshot showed only
the native scene, while the input log confirmed A toggles. The UI pass inherited
back-face culling from the native scene, incompatible with SDL's winding-agnostic
screen geometry after Y conversion. The corrected revision explicitly disables
culling in the UI pass and logs submitted triangle counts only on visibility
transitions. Compiled and uploaded; hardware confirmation is pending.

### SDL texture/font milestone

The user confirmed that disabling UI culling fixed the SDL geometry test.
The next revision implements static/streaming RGBA texture creation, full and
partial SDL_UpdateTexture, vertex-colour/alpha modulation, nearest/linear
sampling, and the real SDL_RenderDebugText atlas. The harness shows an SDL
texture badge, translucent pixels, a font label and a centre patch toggled
red/green with B. A still toggles the whole SDL overlay; Plus exits.

Updates currently use a synchronous correctness-first upload path. Each update
creates an immutable image version; pending CPU command batches and in-flight
frame slots retain the versions they reference. Completed versions are reclaimed
on later uploads, and GPU memory is freed after queue completion at shutdown.
This preserves queued draw/update/draw order and avoids overwriting descriptors
or images still referenced by GPU work. Descriptor capacity and dimensions are
bounded. It is not the final high-performance upload strategy: replace queue-idle
waits with fenced staging and retirement before using animated/movie textures.
Texture render targets and repeat addressing are still explicitly unsupported.

The version compiles successfully with the private-ABI-checked SDL archive.
Hardware confirmation of SDL texture/font rendering is pending.

### SDL render targets and lock/unlock revision

The texture/font milestone was confirmed working by the user. The next revision
adds SDL target textures: queued clear/draw commands retain their destination
image, transitions bind the correct target and viewport, and GPU fragment/image
barriers precede transitions between rendering and sampling. Projection and
clipping use target dimensions. Sampling the active destination is rejected.

Streaming textures now support SDL_LockTexture / SDL_UnlockTexture through a
CPU shadow buffer, preserving the supplied rectangle and full-row byte pitch.
The native immutable-version upload path is reused on unlock. The harness uses
this path for both initial badge data and B's partial red/green update.

The new card is rendered into a 256x96 SDL target and sampled back onto the
window using SDL_RenderTextureRotated. Y toggles straight/12-degree rotation,
B updates the streaming badge, A toggles the overlay, and Plus exits. The build
succeeds; render-target/lock hardware confirmation is pending. SDL flush/native
interleaving beyond this controlled frame graph and the asynchronous upload
optimization remain future work before integration into the full game.

### Borrowed native scene and asynchronous texture uploads

The target/rotation/streaming-lock revision was confirmed working by the user.
The next build exposes a prototype borrowed-scene property on SDL texture
creation. It creates only an SDL wrapper; the native per-frame scene image and
its allocation remain owned by Graphics. At draw recording time the wrapper
selects the acquired slot's existing image descriptor. It is read-only to SDL
CPU updates and cannot be selected as a render target. The harness displays the
native triangle/checkerboard scene again as a small SDL-rendered preview.

Texture updates now submit independent immutable image versions with upload
fences and no queue-idle waits. A collector polls completion, releases finished
upload staging/command allocations, and retires unused image descriptors only
when both pending commands and GPU frame references have released them. At
pool pressure it can wait on an individual frame/upload fence; genuinely live
or pending resources still obey the bounded descriptor limit. The one remaining
queue-idle wait is at shutdown. Frame command-memory reuse remains fenced.
This is an asynchronous versioned allocator, not yet a pooled upload ring.

X toggles continuous streaming badge updates, exercising retirement across many
frames. Startup logging is bounded and the resource summary (uploads, retired,
live and peak image versions) is written once at shutdown, avoiding per-frame
SD diagnostics. A/B/Y/Plus retain their previous controls. Build succeeds;
the sustained-update hardware run completed successfully. Its log reports
1,806 uploads, 1,802 retired versions, four remaining versions and a peak of six,
followed by a clean shutdown. The user completed the run; this confirms bounded
version retention for that workload, not general-game performance.

### First real-model preparation

Added `gt3tool deko-model <disc> <car-vol-path> <output.gtdk>`, using the engine's
existing GT3 car decoder and database wheel specifications. It exports LOD 0
base geometry with reflection passes disabled, mesh transforms, primitive ranges,
texture IDs/flags/alpha thresholds and decoded RGBA textures. It does not export
full lighting materials or replace the production game asset path.

The user's ISO produced `build-switch-deko-model/ma0095.gtdk` for the Mazda 787B:
13 meshes, 4,247 vertices, 3,245 triangles, 54 textures, 2,005,968 bytes. This is
private generated game data under an ignored build directory, not a repository
asset. The host exporter compiles and runs. Drawing this packet on Deko3D is the
next milestone and has not yet been implemented or confirmed.

Packet v1 is little endian: magic `GTDK`, version, mesh/vertex/index/primitive/
texture totals (seven u32 words). Each mesh contains a 16-float column-major
transform, three u32 counts, then local vertices (position3, normal3, UV2 floats,
RGBA u32), local u32 indices and primitive records (first/count/texture/flags u32,
alpha threshold float). Each texture is width/height/flags u32 followed by RGBA8
words. Texture -1 is encoded as 0xffffffff. Flags bit 0/1 clamp U/V; bit 2 disables
mipmaps. A runtime reader must bound counts, ranges, arithmetic and file size.

The lower-level Deko/SDL resource and presentation layers are candidates for
reuse by other native ports, including GT4. The asset decoder and game logic are
separate: GT3 and GT4 do not share an interchangeable model file format. Keep the
engine-specific loader/exporter outside the GPU resource layer.

### First native GT3 model viewer

`tools/switch/deko-model/model.cpp` now reads the bounded GTDK packet, builds
actual vertex/index buffers, uploads its decoded RGBA textures and creates a
private descriptor/sampler table. The viewer draws base opaque/alpha-tested
primitives before basic blended primitives with reversed depth. Additive,
environment and mask passes are omitted. A directional preview light is used;
full car material lighting, reflections, live morph callbacks and production
transparency sorting are not yet implemented. This is a model bring-up viewer,
not gameplay or a faithful showroom renderer.

The camera orbits the transformed mesh bounds. Command/uniform pools are fenced
per frame; model uploads wait only during initial loading. The context restores
its own descriptor tables before sampling the rendered scene through SDL.
The native scene and the SDL miniature now show the Mazda. The UI controls remain
available; A hides it for an unobstructed model screenshot.

The build compiles the engine's existing math3d implementation and two dedicated
model shaders. The NRO expects the privately exported packet at
`sdmc:/switch/gt3r-deko-sdl-probe/ma0095.gtdk`; it does not embed or distribute game
assets. Runtime validation checks version, file size, counts, finite values,
primitive ranges, local indices, texture IDs and extents. Build succeeds;
Hardware rendering is confirmed by the user and screenshot: body, livery and
wheels are visible. The run exited cleanly; 431 uploads, 427 retired versions,
four live versions, peak five. This does not establish full material parity.

### Starting-grid course sector

The exporter also accepts course files, selecting whole meshes whose transformed
XZ bounds intersect a 200 m circle around the first grid slot. Unused textures
are removed and texture IDs remapped. Backdrop and course line are excluded.
This is a static diagnostic subset, not runtime visibility culling.

Route 5 dry exports 51 meshes, 25,736 vertices, 15,862 triangles and 185 textures
in 5,721,932 bytes. The viewer now loads `route5-sector.gtdk` from its SD folder
and orbits the sector bounds, retaining the SDL composition tests. Base lighting
and omitted special passes have the same limitations as the car viewer.
Course rendering was confirmed on hardware, with clean shutdown. The initial
bounds orbit made the scene too small in the screenshot. Course packets now use
version 2: after the seven header words, four finite floats store grid camera
XYZ (2 m above the road) and heading in radians. Version 1 car packets remain
supported. The sector viewer starts at this camera with the UI hidden. Left stick
moves horizontally, right stick looks, L/R lower/raise the camera. Movement uses
elapsed time capped at 50 ms, a dead zone and bounded pitch; there is no collision
or driving simulation. This closer camera requires a new hardware check.

### Combined static scene

The free-camera course run was confirmed by two hardware screenshots and clean
shutdown (five uploads, two retired, three live, peak five). Corrected the yaw
and strafe signs to match the right-handed view basis.

`gt3tool deko-scene <disc> <course-vol-path> <output.gtdk>` builds the same sector
and appends a database-wheel-spec Mazda 787B 12 m ahead of the camera, transforming
its meshes and remapping its texture IDs. This diagnostic placement has no
physics, terrain snapping, driving or dynamic lighting. Auto and course use the
same indexed rendering passes and depth attachment. The combined Route 5 packet
contains 64 meshes, 29,983 vertices, 19,107 triangles and 239 textures, 7,727,888
bytes. The viewer now loads `route5-scene.gtdk`. Compilation and packet range
validation pass; combined-scene appearance awaits hardware verification.

Combined scene is hardware-confirmed: Mazda and course are visible together,
with clean shutdown and the same bounded SDL resource counts. Added bounded
startup/shutdown diagnostics for file read, CPU preparation, initial upload wait,
cumulative allocation bytes (including freed staging), and per-frame CPU recording
and slot-fence wait aggregates. These are not GPU timings or gameplay FPS. No
per-frame SD logging is added; frame aggregates remain in RAM until shutdown.

Hardware measurement of the combined scene: 7,727,888-byte packet read in
101.52 ms, CPU preparation 154.87 ms, initial upload/queue wait 1.61 ms, total
model loading 294.14 ms. This excludes device/window/shader/SDL initialization.
Across 2,051 submitted frames CPU recording averaged 0.265 ms (maximum 0.533 ms);
slot-fence waits averaged below the 0.001 ms display precision, maximum 0.001 ms.
The last frame issued 819 draws / 18,850 triangles. Cumulative model allocations
were 19,509,248 bytes including staging already freed, not resident/peak memory.
Shutdown was clean, SDL versions uploads=5 retired=2 live=3 peak=5. These values
describe this static subset and do not measure GPU execution, frame rate or a
full race. There is no comparable timed earlier startup, so the reason for the
user-observed faster launch is not established.

### Material coefficients and vertex lighting

GTDK version 3 adds a camera-present u32 after the seven header words (followed
by four camera floats when set). Each primitive's existing 20-byte record is
followed by 16 floats: ambient RGB / valid, diffuse RGB / scale, specular RGB /
shininess, emission RGB / reserved. Versions 1 and 2 remain readable. Material
values are finite checked with bounded scale/exponent. Specular-local course
materials are not activated by this generic material path. Brake-lamp emission
and original scene light data are not yet exported.

The vertex shader now evaluates valid lit materials using the engine's ambient,
diffuse, Blinn specular and emission formula, quantized exponent (4/8/16),
material scale, PS2 colour clamp and untextured factor. It retains the demo's
fixed directional/ambient light and infinite-viewer direction, so this is not
production lighting parity. Prelit course primitives retain their vertex colours.
Legacy or invalid materials retain the generic preview path. Environment passes
and reflections remain omitted. Uniforms occupy 224 bytes in each fenced
256-byte draw slot, with identical std140 layouts in both stages.

ZL toggles original material coefficients versus generic preview illumination
without a reload; only the toggle is logged. The combined packet contains 94
valid lit material records, including 13 with specular coefficients; geometry
and 239 textures are unchanged. Version-3 packet size is 7,780,884 bytes. Host
exporter, Switch C++ and both shaders compile; packet ranges/material values
validate. Hardware appearance of the new material path remains to be checked.

Hardware material comparison completed: both screenshots show the same camera
with intact livery, body and wheels; differences are subtle with the preview
light, most evident in wheel/body shading. The log confirms repeated material
toggles and clean shutdown, but does not timestamp screenshots, so their exact
mode order is not established from the log. Loading took 296.74 ms (read 103.06,
prepare 155.05, upload wait 1.61). Across 3,222 frames CPU recording averaged
0.911 ms, max 1.054 ms, versus 0.265 ms in the earlier run. This cross-run
increase needs investigation; it is not a controlled CPU-clock comparison or
a GPU performance measurement. Slot wait max 0.002 ms; SDL resource counts
remain uploads=5 retired=2 live=3 peak=5. Environment reflections still absent.

### CPU recording investigation

The material comparison's higher CPU recording time is not yet causally explained.
The next build reports separate generic/material aggregates within one process.
Removed redundant depth/blend/texture state commands, computed each mesh MVP
once per pass and the viewer direction once per frame. Generic/prelit draws push
160 bytes (including zero material-validity) rather than the full 224-byte
material block; only lit valid materials send the tail that the shaders consume.
No primitives are merged or reordered. Per-frame uniform-byte and state-change
counts are aggregated with no per-frame file I/O. Build succeeds; visual parity
and CPU impact require hardware verification before adding environment passes.

CPU optimization hardware run: 2,553 frames, recording average 0.499 ms, maximum
0.557 ms. Within-run generic mode averaged 0.498 ms (1,089 frames) and materials
0.499 ms (1,464 frames), so material toggling alone shows no material CPU
recording difference here. Across runs recording decreased from 0.911 ms, but
slot-fence wait increased to average 2.742 ms / max 3.065 ms, versus essentially
zero previously. This prevents interpreting lower recording time as an overall
frame-rate improvement. Need full presentation/frame cadence measurements to
distinguish a shifted wait from a regression; fence wait is not GPU timing.
Last generic frame: 819 draws, 18,850 triangles, 131,040 uniform bytes and 568
state changes. Load total 293.35 ms, clean shutdown, SDL version counts stable.

### Whole-frame wall-time instrumentation

Added RAM-only bounded timing histograms (0.25 ms bins, final overflow bin at
255.75 ms), separated by material mode. At shutdown report count, mean, p95/p99
bin upper bounds (saturated when overflow is nonzero), maximum and counts beyond
16.67/33.33 ms. Native phases cover swapchain acquire, scene submission (including
the model slot wait), composition/SDL work, queue present and total native present.
Return-to-return present cadence includes intervening application work/pacing;
mode-transition cadence samples are excluded. The harness also measures its
event/input/render loop and SDL_RenderPresent call, excluding first/toggle frames.
These are CPU wall times, not GPU queries or display-delivered frame counts.
No additional per-frame file I/O, waits or queue submissions are introduced.
Build passes; hardware measurements remain pending.

Whole-frame hardware run (2,635 frames): generic app loop averaged 16.666 ms,
materials 16.652 ms. Present-return cadence averaged 16.667 / 16.661 ms,
consistent with approximately 60 Hz pacing. Swapchain acquire accounts for
16.261 / 16.248 ms on average. Model CPU recording averaged 0.253 / 0.241 ms,
with slot-fence wait effectively zero (max 0.001 ms). App-loop maxima were
16.971 / 16.909 ms; no recorded loop/cadence sample exceeded 33.33 ms.
Material cadence maximum was 26.869 ms; initialization remains in native
phase/cadence aggregates, including a 13.857 ms compose/UI maximum. Counters
just above 16.6667 ms should not be interpreted as dropped display frames.
The earlier 2.742 ms slot wait did not recur; this run supports stable paced
presentation for this fixed subset, not its cause in prior runs or a claim
about GPU headroom/full-race speed. Loading 288.32 ms, clean shutdown.

### First weighted reflection pass (gradient environment)

Car decoding now includes its environment-map geometry. GTDK v4 appends eight
floats per primitive after the material: C0 RGB / rows-valid, C1 RGB / reserved.
The reader retains versions 1–3 and checks finite values and row validity.
The new third render pass accepts only additive+environment primitives, uses
GEQUAL depth with writes disabled, SRC_ALPHA/ONE RGB blending and preserves
destination alpha. Shader colour follows the existing renderer's reflection
fallback: reflected view vector samples a sky/horizon/ground gradient, weighted
by original C0+C1*min(V.N,0), vertex colour, 0.4 strength and base texture alpha.
This does not yet reflect the actual scene, use a generated cubemap/sphere map,
match course lighting/fog or reproduce the full PS2 lookup. The gradient colours
are the renderer's default preview environment, not Route 5 lighting data.

ZR toggles reflections independently of ZL materials, with toggle-only logging.
Reflection uniforms use the full 256-byte fenced slot; material draws remain
224 bytes, generic/prelit draws 160 bytes. The shader branches on params.z
before accessing the longer reflection tail. Geometry/indices/texture ranges
validate: 64 meshes, 36,449 vertices, 20,793 triangles, 910 primitives, 239 textures,
8,069,900 bytes, including 82 reflection primitives / 1,686 triangles.
Build succeeds; appearance and occlusion of the new pass require hardware
confirmation. Existing timing aggregates separate materials only, so runs
toggling ZR mix reflection-on/off performance and are visual checks only.

Reflection hardware A/B exposed a full-image purple cast, including road and
buildings. The compositor inherited SRC_ALPHA/ONE blending from the model's
last reflection pass, adding the swapchain diagnostic purple clear colour to
the scene. Reflection-off ended with standard alpha blending and did not show
that global cast. Fixed the final composition command lists to explicitly bind
blend-disabled colour state, default blend factors/write mask, no-cull raster,
full viewport/scissor and depth-disabled state. The purple clear remains as a
diagnostic: an opaque overwrite must hide it. This changes final composition,
not the reflection gradient or material coefficients. Build passes; corrected
reflection-on/off hardware comparison is pending.

Corrected reflection composition confirmed by hardware A/B screenshots: road
and buildings retain their colours; changes are localized to the car's reflective
surfaces. The environment remains a gradient, not actual scene imagery. The log
shows reflection toggles and clean shutdown, load 311.62 ms, mean present cadence
16.662 ms across 1,409 intervals. One interval reached 36.540 ms (one above
33.33 ms); it cannot be attributed from these aggregates, which include ZR
toggle/logging intervals. CPU recording averaged 0.320 ms and slot wait max
0.001 ms. This mixed on/off run is not an isolated reflection performance test.

### Static six-direction scene environment

GTDK v5 adds a mesh-role u32 after each mesh transform, before its counts
(0 course, 1 car). The combined export validates 51 course and 13 car meshes,
8,070,156 bytes; older packet versions remain readable. On v5 course scenes
the loader allocates six sampled/renderable RGBA8 128x128 images and one shared
Z24X8 capture depth target. The capture origin is the static diagnostic car
placement, one metre above its grid position. Six 90-degree views draw course
meshes only, excluding car geometry and reflection passes. Startup submissions
flush their completion fences before reusing slot 0 command/uniform memory;
a final initialization-only idle wait completes the capture. Runtime sampling
uses six clamp samplers with a dominant-axis cubemap lookup (separate 2D images),
and the existing weighted additive reflection pass. No CPU image readback.

Capture targets are owned by the model texture list; descriptors include all six
faces. Capture barriers order target/depth reuse and texture sampling; the main
graphics context binds its scene target before each normal frame. Captures do
not enter steady CPU-frame aggregates. The log reports capture wall time once.
ZR still toggles the full reflection pass. The gradient remains only a fallback
when a captured environment is unavailable. This is a fixed low-resolution
scene capture: no dynamic updates, seamless cube filtering, PS2 sphere-map
lookup/5-bit quantization, real backdrop, fog or full scene-light parity yet.
Shader and C++ builds pass; face orientation, colours and runtime operation
need hardware verification.

First automatic capture hardware run produced no image. The log ends at the
graphics texture submission, before model-load completion, with no debug error;
it does not identify the exact failing stage. Added bounded startup checkpoints
around file reading, upload and each capture face. Capture now uses a dedicated
fragment shader without environment samplers, and normal rendering binds valid
white fallback environment descriptors before a map exists. These address
possible unbound/self-sampling dependencies but the cause is not yet confirmed.

For isolation, automatic capture is disabled: Minus explicitly performs the
six-view capture after the scene starts. Each face is synchronized during this
diagnostic operation (not during normal frames). This is a diagnostic build,
not a confirmed fix. The scene should initially use the prior gradient; the
user can test startup and capture separately.

Manual-capture build is hardware-confirmed operational. The log records two
complete six-face captures (195.59 and 200.02 ms), followed by clean shutdown.
Normal rendering issued 901 draws / 20,536 triangles with CPU recording average
0.307 ms and max 0.566 ms. Native presentation averaged 16.602 ms; the app-loop
and cadence aggregates include the two explicit captures (~233 ms peak), so
they are not steady-state frame-rate estimates. SDL counts remain bounded
(uploads=5 retired=2 live=3 peak=5). This confirms capture completion and continued
rendering, not face orientation/image fidelity or which isolation change fixed
the prior startup failure. Automatic capture remains disabled pending further
verification; visual comparison of the captured environment is still needed.

Captured-environment visual A/B received (20261102210310/14): reflection-on
adds localized highlights/patterning on the body and glazing; road/building
colours stay unchanged and there is no global purple cast. Effect is subtle
in the night scene. These two stills do not establish all six face orientations
or parity with the original renderer. Log confirms one complete 200.06 ms
capture followed by reflection-off and clean shutdown. CPU recording averaged
0.321 ms, slot wait max 0.002 ms. Cadence includes the explicit capture stall
(~234 ms max), so do not interpret its mean as steady-state reflection FPS.

### Deferred automatic static capture

The harness now invokes the verified manual capture path once after three
SDL_RenderPresent calls have completed. It does not capture inside model/device
initialization. A manual Minus request before automatic capture cancels the
pending automatic request; subsequent Minus presses explicitly recapture.
Exit is processed before either capture trigger. The camera time baseline is
reset after the capture so its pause does not enter movement integration.

Capture wall time is reported separately as explicit_capture_pause/setup. Its
app-loop sample is excluded from normal-loop aggregates and the native cadence
baseline is reset, excluding the return-to-return interval crossing that pause.
This keeps the ~200 ms synchronous setup cost visible without mixing it with
steady cadence. This is still one blocking static capture, not incremental or
dynamic environment rendering. Build passes; deferred startup needs hardware
confirmation. Experimental NRO metadata is 0.6.0-envauto.

Deferred automatic capture confirmed on hardware: exactly one automatic request
and six completed faces after the first visible frames. Capture internal wall
time 54.94 ms, full explicit pause 59.011 ms; this run does not explain the
difference from earlier ~200 ms captures. Across 2,574 frames, normal material
app-loop mean 16.646 ms / max 16.919 ms, cadence mean 16.649 ms; generic mode
was briefly toggled (122 frames). No measured normal loop or cadence exceeded
33.33 ms after excluding the explicit capture pause. CPU recording averaged
0.306 ms; slot wait max 0.001 ms. Clean shutdown and bounded SDL resource
counts. This validates deferred static startup capture, not dynamic updates
or full-race performance.

### Engine CPU scene upload boundary

Added `Model::load_scene(const scene::Model&, const SceneOptions&)`. The native
uploader now consumes the engine's CPU scene contract directly (no SDL_GPU
handles and no serialization). It validates counts, indices/primitive ranges,
transforms/vertices/materials, texture extents and explicit per-mesh capture
roles, flattens geometry, and owns the resulting GPU resources. Input CPU data
is borrowed only during the call; no scene pointers are retained. This is still
a one-shot bounded uploader, not dynamic geometry/texture updates or a complete
GpuModel/View3D backend. Invalid loading leaves the object disposable, not
transactionally reusable. Production GLES code/build selection is untouched.

The diagnostic GTDK reader is now a CPU adapter: it reconstructs scene::Model
and SceneOptions from versions 1–5, validates packet totals/end, releases file
bytes, and calls the same load_scene entry point. The hardware demo therefore
exercises the actual typed uploader; it still reads its private diagnostic
packet, not the ISO or game loader. This transient CPU reconstruction adds
loading work/memory and is not a runtime upload optimization. Logging now
separates packet-adapter timing from engine-scene upload timing. Experimental
build uses C++20, matching the engine scene header's std::span dependency.
Build succeeds; hardware parity after this refactor remains to be checked.

Typed-uploader hardware run completed: packet reconstructed scene::Model,
uploaded 64 meshes / 36,449 vertices / 20,793 triangles / 910 primitives / 239
textures, with matching previous scene totals. Engine-scene upload 186.30 ms
(including 144.39 ms preparation / 1.62 ms upload wait); total packet path
335.24 ms. One deferred capture completed in 67.53 ms and shutdown was clean.
Across 625 frames, normal app-loop mean 16.581 ms / max 17.042 ms, cadence mean
16.590 ms / max 17.043 ms, no recorded normal sample over 33.33 ms. CPU recording
was 0.579 ms and slot wait 3.506 ms average (max 4.673 ms), higher than the prior
run; do not claim equivalent CPU costs or identify a cause from this cross-run
comparison. Whole-frame cadence remains approximately 60 Hz for the subset.
SDL resource counts remained uploads=5 retired=2 live=3 peak=5.

### Same-topology dynamic vertex updates

Added typed `Model::update_vertices(meshIndex, span<const scene::Vertex>)`: rejects
unknown meshes/count mismatches/nonfinite vertex data before changing the CPU
shadow. Indices, primitive ranges, materials and textures remain fixed. CPU
input is copied before return. Per-mesh versions track dirty ranges. Each of
two frame slots lazily allocates its own vertex buffer on the first update,
initially copies the full shadow, and thereafter copies only meshes with a
new version after its completion fence is satisfied. The command list invalidates
L2 before fetching changed vertices. Buffers stay owned until shutdown; normal
updates add no queue-idle wait or per-update allocation after both slots exist.
The original static buffer remains untouched. This does not yet resize geometry,
update textures, recalculate visibility bounds or implement animation callbacks.

The diagnostic harness animates car vertices up/down by a 0.4 m sinusoid, using
the typed update API for all car meshes, including reflection geometry. A world
vertical offset is converted to each mesh's local space so body/wheels remain
together. It retains original CPU vertices and reuses a scratch vector. D-pad
Up disables the animation and restores base vertices, or enables it again.
This is GPU lifetime/update verification, not suspension/physics. Startup
reflection capture still excludes the car; its map remains static. The log
summarizes update calls, GPU copy bytes and dynamic buffer allocation count
once at shutdown. Build succeeds; sustained hardware animation remains pending.

Dynamic-vertex hardware verification passed visually per user. Log confirms
23,426 per-mesh updates, two off/on cycles with base-vertex restoration, exactly
two dynamic slot allocations and 697,598,064 cumulative copied bytes (traffic,
not resident memory). Across 1,914 frames normal app loop averaged 16.646 ms /
max 17.607 ms and cadence averaged 16.650 ms / max 17.608 ms, with no normal
sample beyond 33.33 ms. CPU recording including slot copies averaged 0.705 ms,
max 1.330 ms; slot wait max 0.003 ms. The CPU diagnostic animation itself is
additional app work, so this does not isolate a real-game animation cost.
Capture completed in 72.65 ms, shutdown clean, SDL resource counts unchanged.

### Same-size dynamic RGBA texture updates

Added typed `Model::update_texture(index, span<const uint32_t>)`. It validates
the original CPU texture index and exact pixel count, copies pixels before
returning, and increments a per-texture version. Each frame slot lazily owns
one replacement GPU image and persistent upload allocation per updated texture,
plus its own copy of the image/sampler descriptor table. After that slot's
model fence completes, its upload bytes/descriptors can be written safely.
Buffer-to-image commands precede draws; L2/image/descriptor invalidation makes
updated data visible. Other in-flight slots retain their own images/descriptors.
No normal-update queue-idle wait, per-update GPU allocation or changes to the
static source textures. Resources are retained until model shutdown. Same-size
RGBA only: resize, partial updates, mip rebuilding and cross-model sharing
remain outside this first API.

The harness continuously modulates green/blue of its largest car texture (ID
223 in this scene, 512x128), preserving red and alpha. D-pad Down stops updates
and restores original pixels; pressing it again resumes. Original pixels and
CPU scratch are retained only for this diagnostic. D-pad Up keeps controlling
the independent vertex demo. GPU copy bytes and persistent image/upload/table
allocation counts are summarized at shutdown. C++/shader build succeeds without
warnings; sustained hardware texture updates and restoration need verification.

Dynamic-texture hardware run passed visually per user: 1,954 update calls,
two off/on cycles restoring original pixels. Persistent allocations remained
exactly two replacement images, two upload buffers and two descriptor tables;
512,753,664 cumulative copy bytes is traffic, not resident memory. Vertex
updates also continued with exactly two vertex slots. Across 2,215 frames,
normal app loop averaged 16.646 ms / max 17.794 ms; cadence averaged 16.650 ms
/ max 20.118 ms, no normal sample above 33.33 ms. CPU recording averaged
0.866 ms, max 4.066 ms, slot wait averaged 2.814 ms (max 4.243 ms); costs are
not isolated from simultaneous vertex animation and CPU colour modulation.
One environment capture completed in 72.10 ms; clean shutdown and unchanged
SDL version counts. This validates same-size model texture updates for the
diagnostic workload, not all race animation or resource-lifetime patterns.

### Per-frame object transforms and external camera boundary

Added finite-validated `set_mesh_transform(index, Mat4)` and `set_camera(view,
projection, eye)` APIs. Transform state is copied into existing fenced per-draw
uniforms; it does not rewrite geometry or texture resources. The external camera
(overriding the preview camera for normal views) uses the same matrix/eye types
as View3D. Capture views keep their own six directions. These are input
boundaries; the production DrawItem/GpuModel/race code is not wired yet. Current
DrawItem embeds a GpuModel pointer, and CourseScene/CarVisual construct those
objects directly. A race backend adapter must address that ownership, dynamic
geometry/topology, lighting flags, camera/input and session initialization.

The diagnostic now defaults to a rigid 2 m forward/back car motion through
mesh matrices, with initial transforms retained. D-pad Right restores base
transforms or resumes; CPU vertex/texture diagnostic animations default off
(their Up/Down controls remain). Original camera yaw fixes the motion axis so
looking around does not steer the diagnostic. All car meshes, including wheels
and reflection geometry, receive the same world translation. The captured
environment remains fixed and is not relocated with this animation. Shutdown
logs transform/camera update counts. Build passes; transform hardware parity
and external-camera use by race code are still pending. A playable prototype
requires actual simulation/terrain/session integration, not kinematic motion.

Object-transform test completed: user reports correct forward/back motion; log
records 4,979 matrix updates, no geometry reupload, normal app-loop average
16.563 ms / maximum 16.916 ms, clean shutdown. Branch verified experiment-deko3d.

### VehicleState and engine CameraRig bridge

Added initial_vehicle_state and apply_vehicle_state: the engine's world-space
VehicleState position/quaternion produces a normalized rigid pose delta from
the diagnostic placement and updates every car mesh's original matrix. This
currently treats wheels rigidly with the body; independent steer/spin/suspension
transforms still need real CarVisual integration.

The harness now links the current engine archive, rebuilt via target gt3r_engine
without replacing the production app NRO. It uses the actual race::CameraRig
in ChaseFar mode (shake disabled) and submits its view/projection/eye through
the external-camera API each frame. A synthetic sinusoidal VehicleState still
supplies motion/velocity. VehicleSetup is default preview data, not database
setup, and no RaceVehicle/ExactRace or terrain/session logic runs yet. Left-stick
click toggles chase/free view; Right toggles synthetic motion. Input-free camera
updates use an empty engine pad, and dt is capped after capture/loading pauses.
Build and link pass; chase framing and continued rendering need hardware
verification. This is a state/camera adapter, not a playable physics demo.

VehicleState/CameraRig bridge hardware-confirmed: screenshot shows the 787B
framed from behind on the starting straight. Log records 17,238 transform
updates and 1,137 external camera updates, with a free/chase toggle each way,
normal app-loop mean 16.637 ms / max 19.290 ms and clean shutdown. Vehicle
motion remains synthetic. The minimal real session should reuse ExactRace
initialization (ProgramData, CarDatabase, RaceCourse/ground, event/config and
StartEntry equipment), then feed actual player VehicleState/VehicleSetup into
this tested adapter. These assets/session prerequisites are separate from the
private GTDK render packet, whose selected meshes alone cannot provide physics.

### Minimal live ExactRace session

Added renderer-independent tools/switch/deko-session/Session, shared by the
host diagnostic tool and experimental Switch harness. `gt3tool deko-session
<disc> <output.gtds>` captures the database reader's needed files plus the
user's original program and full Route 5 dry course into a bounded private
bundle (8 entries / 13,522,415 bytes on this NTSC-U ISO). This is generated
game data under ignored build output; never commit or distribute it.

The session loads CarDatabase and recognized ProgramData, builds RaceCourse
(ground/collision/AI queries), fits the Mazda _787b_91 equipment, and initializes
one human player on grid slot 0 using the SinglePlayer conductor, standing
start with no countdown and one lap. Rules are a zero-initialized diagnostic
RaceConfig; this is not a normal event/session setup. The FreeRun conductor
was unsuitable here because its flying start put the car outside the rendered
sector; SinglePlayer starts at (-235.86, ~0, -2.64), inside the existing scene.

Host smoke test (same bundle/parser/session as Switch): 300 ticks at half
throttle moved 36.10 m and reached 56.24 km/h; 120 full-brake ticks then reduced
speed to 0.00 km/h. The exporter refuses to save if these checks fail.

The Switch loads route5-physics.gtds beside the render packet, advances actual
ExactRace at fixed 60 Hz (at most 3 ticks per render iteration, frame delta
capped at 50 ms), interpolates VehicleState, and supplies actual VehicleSetup
to CameraRig. ZR throttle / ZL brake / left stick steering; right-stick click
rebuilds the session on the grid. A speed label is drawn through SDL. Left-stick
click retains chase/free camera (steering disabled while in free view). Previous
synthetic animation triggers are disabled. Scene capture is centred 1 m above
the actual spawn. Exceptions are logged and normal renderer cleanup applies.

This first control test is limited to the existing 200 m mesh-selected sector:
physics has full track data, graphics do not. No audio, real event HUD/menu flow,
independent visual wheel steer/spin/suspension, moving reflection capture or
complete material/lighting parity yet. Static car mesh bind pose is transformed
by the physics pose. Engine archive and compression dependencies link in the
separate probe build; production NRO is not replaced. Host smoke and Switch
build pass; live driving on hardware is pending.

First real-physics Switch drive passed per user. Log confirms ExactRace setup,
one grid reset, 1,000 ticks in the final session (counter restarts on reset),
maximum observed speed 164.98 km/h and final speed 0.00 km/h. Renderer recorded
27,521 transform updates and 2,117 camera updates; shutdown was clean. Normal
app-loop mean 16.734 ms / p95 and p99 histogram upper bound 17.00 ms, with one
233.194 ms maximum. Reset rebuild cost is not excluded from these aggregates,
but the log does not independently attribute that spike to reset. Capture
pause is separately excluded. This validates the live-control prototype on
the starting sector, not a full lap or a complete game renderer. Next: full
course geometry/resource limits/visibility, independent wheel rendering,
actual scene lighting and animation/topology updates.

### Complete static Route 5 course

Added `gt3tool deko-track <disc> <course-vol-path> <output.gtdk>`; unlike the
sector command it retains all static course meshes (still excludes backdrop
and course-line helpers) and appends the Mazda. Full Route 5 packet validates:
273 course + 13 car meshes, 166,337 vertices, 104,983 triangles, 4,252 primitives,
499 textures, 14,920,704 RGBA bytes, 22,690,780 file bytes. Runtime texture limit
is now 1,024, primitive limit 8,192; per-slot command allocation is 4 MiB and
uniform allocation 2 MiB. Total packet remains under its 64 MiB reader bound.

Added CPU view culling for course meshes, using local AABB bounds transformed
conservatively to world AABBs, six clip planes for reversed 0..1 depth, and a
600 m distance limit accounting for bound extent. Visibility is computed once
per view before all three passes, including each environment face. Car meshes
bypass this course culling. Same-topology vertex updates refresh local bounds.
No mesh is removed from loaded course data; moving around the full track changes
which resident meshes are submitted. Whole-course bounds need not fit a sector.

Host-only visibility_test.cpp passes 14 checks for front/back, near/far clipping,
edge-straddling boxes, centre-outside/intersecting bounds, reflected/scaled
transforms, a rotated/translated camera and conservative distance. Compile via
clang++ -std=c++20 -Isrc tools/switch/deko-model/visibility_test.cpp
src/render/math3d.cpp. Full-packet finite/index/material/texture bounds also
validate; Switch build passes. Viewer now loads route5-full.gtdk. Physics
bundle/control scheme unchanged. Full-lap appearance and performance need
hardware validation; animated scenery/backdrop/material completeness and
independent wheel rendering remain pending.

### Full-lap verification, unlimited test and city/palette restoration

User completed the first lap; the car slowed and drove itself because the
diagnostic session was configured for one lap. Changed SinglePlayer laps to
0: race_mode_lap explicitly checks nonzero in.laps before judging a lap finish;
no time limit or opponents exist here. This retains the standing-grid start
without FreeRun's flying start. Session shutdown now logs lap counter and
finished flag to verify continued control. Hardware full-track run was clean:
7,788 frames/ticks, max 296.05 km/h, app-loop mean 16.660 ms / max 17.289 ms,
no normal frame over 33.33 ms; 1,700,438 of 2,126,124 course-mesh tests culled.

The old OpenGL animation configuration is not read by this probe. Missing
animation was unimplemented integration; missing distant city was an explicit
export exclusion. Full export now appends slot-22 backdrop and preserves the
original uncompacted course texture order. GTDK v6 adds role 2 for distant
backdrop (0 scenery, 1 car), retaining prior packet versions. Backdrop is
frustum-culled but exempt from the 600 m course distance. 12 backdrop meshes
add only 36 triangles. New route5-city.gtdk validates 298 meshes / 166,405
vertices / 105,019 triangles / 4,268 primitives / 502 textures / 24,269,376 bytes.

Session retains the original course bytes. The model reconstructs course CLUT
sets with the engine's decoder, checks texture layout against the uploaded
course, then advances ClutPlayer at physics tick rate and submits refreshed
RGBA via the verified per-slot update_texture API. Host check finds 9 sets /
25 animated records / 448 course textures, matching the uncompacted export
before car textures. Grid reset restarts their players; Down freezes/resumes
original palette animations. These are actual CLUT animations, not the earlier
synthetic colour diagnostic. Moving-node geometry remains static: its node
animation and topology path is still pending. Sky domes and some environment
passes remain outside current parity. Host acceleration/brake smoke still
passes, packet ranges and Switch build pass. Hardware skyline, CLUT changes
and continuation after the first lap need verification.

### Original night-course car lighting

Reuse CoursePointLights and SunParams from Env0/GTRW. The physics session keeps
the front-left wheel's road attribute A lamp index, preserving it on a missed
road query, as RaceScreen does. Each rendered VehicleState selects up to four
lamps among the original candidates using the engine's attenuation/spot rules.
The model combines global ambient and weighted per-lamp ambient/diffuse/specular
colours. A dedicated vertex-stage binding-1 UBO (208 bytes in a fenced 256-byte
slot) sends four directions and colour triples once per view. Valid lit car
materials use the existing per-vertex formula with these values; scenery and
prelit/unlit packets keep their prior path. Generic preview lighting remains
available through D-pad Right. This does not yet add lamp shadow projections,
flare/halo sprites, reflection-map light discs, headlamp/stop effects, full
course-light handling for lit scenery or braking emission.

Host smoke detects 281 lamps, valid Env0 and up to four selected lights over
300 physics ticks, checking finite directions/nonnegative finite weights.
Acceleration/brake checks still pass. Switch shader/C++ build succeeds without
warnings. Shutdown logs selected-light update count and maximum selection;
no per-frame file I/O. Hardware variation under lamps and generic/original
lighting comparison remain pending.

Night car-lighting hardware A/B received: rear car shading changes while track
colours remain consistent; screenshots have slight position drift, so they
are not a pixel-identical comparison and mode order is not timestamped. User
reports correct operation. Log confirms 281 loaded lamps, 3,922 lighting
updates, up to four selected lights, repeated original/generic toggles and
clean shutdown. Normal app-loop mean 16.661 ms, max 33.769 ms with one sample
over 33.33 ms. Palette animation continued (62,995 uploads). This supports
functional original point-light selection/material shading in the prototype,
not full lighting parity or isolated performance.

### Original brake-lens node branch and emission

Brake history now follows the actual exact-core brake_pedal over four ticks,
matching RaceScreen's factor k/4 and current pressed state. The CPU scene already
contains lamp_emission but the initial diagnostic packet dropped it. GTDK v7
introduced three lamp-emission floats after each primitive's environment rows;
v8 retains these and adds mesh role 3 for a braking car-node variant. Earlier
versions remain readable. The original 787B base branch has no lamp emission;
its brake=true node-program branch changes geometry and supplies one emissive
material. Full export therefore appends both decoded car variants, each with
its own textures. This is temporary prototype duplication, not runtime loading
per brake press or a complete dynamic-node implementation.

Both car variants receive the same VehicleState transform. Visibility selects
exactly one entire car variant from current brake state; both remain excluded
from the environment capture. Valid lit car materials add lamp_emission x
k/4 before vertex lighting. Original branch selection is immediate while the
emission factor uses the history. No generated red tint or overlay is used.

route5-brakes.gtdk validates 311 meshes (273 course / 12 backdrop / 13 base car /
13 braking car), 177,126 vertices, 109,954 triangles, 4,453 primitives, 557
textures, 26,605,676 bytes. Base/brake vertex data differ; one braking-variant
primitive has nonzero lamp emission. Source palette indices remain unchanged.
Host physics and lamp-selection tests pass; braking history is 1.00 under
full braking and 0.00 after release. Switch build passes without warnings.
This step covers stop lens/material activation only: CarLampGlows head/tail
sprites, halos and their occlusion still need integration. Hardware stop
appearance and switching consistency are pending.

Brake-lens hardware test passed per user. Log records 515 frames with positive
brake emission factor, maximum factor 1.00, final car stopped and clean shutdown.
Normal app-loop mean 16.657 ms, max 35.725 ms (one sample over 33.33 ms); CPU
recording max 31.099 ms shows a transient that is not attributed by these
aggregates. This confirms visible lens activation, not completion of glow/halo
or a guarantee of unchanged performance.

### Car head/tail/brake glow integration

The private physics bundle now includes the original Mazda car file (GTCI) and
race/flare.bin, 10 entries / 14,057,139 bytes. Host checks read five head, two
tail and two brake records and a valid 2,720-byte original flare. Existing
CarLampGlows code performs placement, viewing-angle/attenuation, brake-history
weights, pulled-point halo tests and flare geometry. The SDL Deko backend now
accepts SDL_BLENDMODE_ADD and uses SRC_ALPHA/ONE RGB with destination alpha
preserved, alongside its previous NONE/BLEND paths.

Physics/camera preparation moved before SDL overlay queueing so glows project
using exactly the current interpolated VehicleState/CameraRig. Native present
then renders that prepared state. App-loop timing includes preparation; native
presentation phase timing no longer includes physics preparation, so compare
those phase values accordingly. Glow projection uses the compositor's actual
inset scene rectangle (4% margin / 92% dimensions), not the whole window.

Course/backdrop opaque triangles build an Occluder from the loaded CPU scene,
excluding both car variants. A separate opaque-body blocker is built from the
original car, excluding glass/cutouts. CarLampGlows rays test both, as the
existing renderer does; no untested overlay-through-body shortcut. Host smoke
using the original flare and opaque body produces 536 front-view headlamp
triangles and zero rear-view triangles, with a 474-triangle body blocker.
Physics, braking history and lamp selection smoke tests still pass.

Glows use SDL ScreenBatch geometry after scene composition, before HUD, in
chase/look-back views. Hold L for the actual CameraRig rear-facing view; glows
are currently skipped in free camera because that debug camera's projection
is not exposed to this adapter yet. No headlight road beams, streetlamp halos
or GPU depth readback are introduced. The ray tests handle this single-car
prototype; future multiple moving cars need their blockers added.

CarLampGlows SDL resources are released before the SDL renderer, including
exception unwinding. The log reports glow-frame and triangle totals at exit.
Switch build succeeds without warnings; glow visibility/alignment and cost
remain to be checked on hardware. This is the final scoped change for today's
work; do not infer confirmation of untested hardware behaviour.

L control correction: hardware user reported no camera change. ChaseFar ignores
CameraRig::look_back; the harness had selected that mode. L now transitions to
Chase (which applies the look-back camera frame), enables follow view, and
returns to ChaseFar on release. Mode changes occur only on transitions.
This corrects the earlier L instructions; hardware front-lamp view is pending.

### Stock-clock test baseline and wheel animation

User reports disabling overclock and observing approximately 60 fps in the
previous prototype; future hardware runs use OC-off as the reported baseline.
Clock frequencies have not been queried, and this is not a measurement across
other tracks or a multi-car race. Startup log labels this as user-reported.

GTDK v9 adds wheel ownership after each mesh role: u32 wheel index (-1 none),
u32 spin flag and three float bind-hub coordinates, before mesh counts. Names
from the car decoder identify FL/FR/RL/RR rim, tyre and band/caliper meshes;
hubs come from the same database WheelSpec used to place the geometry. Both
base/braking variants carry these bindings. Older packet versions remain
readable and keep rigid-car rendering. New route5-wheels.gtdk validates all
indices/materials/textures, 24 wheel meshes (16 spinning / 8 nonspinning),
26,611,896 bytes, with unchanged vertex/triangle/texture totals.

VehicleState is now applied together with VehicleSetup. Per-wheel transforms
use bind X/Z, simulated rest hub height + suspension travel, negative steering
yaw, side-correct camber and negative wheel spin. Since exported vertices have
placement baked in, transforms subtract the original hub before applying the
new hub frame; band/caliper meshes omit spin. Geometry/texture uploads are not
needed. Body meshes gain the same static ride-height lift as CarVisual; lamp
projection/body blockers gain the identical lift so glows stay aligned.
D-pad Up toggles wheel motion versus the static bind pose; physics is unchanged.
Independent wheel blur/spinning-image effects and brake heat remain pending.

Host wheel_pose_test.cpp passes 12 checks for neutral pose, hub invariance,
quarter-turn spin, nonspinning bands, steering, suspension travel, left/right
camber, wrapped spin and body lift/default. Build via clang++ -std=c++20 -Isrc
tools/switch/deko-model/wheel_pose_test.cpp src/render/math3d.cpp. Switch build
passes without warnings. Shutdown reports wheel mesh updates and max steer,
travel and spin step. Hardware alignment/rotation after this change remains
pending, using the user's stock-clock baseline.

Wheel-motion hardware test passed visually per user, with OC disabled as
reported (not clock-queried). Log confirms off/on comparison, 173,064 wheel-mesh
updates, max steer 0.846 rad, suspension travel 0.074 m and spin step 2.968 rad.
Car glows remained active with 229,666 submitted triangles across 4,593 glow
frames. Normal app-loop histogram: 7,180 samples, mean 16.760 ms, p95/p99 bin
upper bound 17.00 ms, max 372.314 ms and three samples over 33.33 ms (two in
255.75 ms+ overflow bin). Reset/setup costs are not all excluded; these
aggregates do not identify individual hitch causes. Shutdown clean. This
confirms wheel animation and generally near-60 Hz cadence for this workload,
not hitch-free or full-game performance at stock clocks.

### Original street-lamp glow pass

Added LampGlows from original GTFX slot 29, Env0 types, VLS0 visibility lists
and the already bundled race/flare.bin. Reuses source Switch limits (35 kept
lamps, maximum 450 m) and the production default visibility interval of two
frames, with its cache invalidation on view changes. Blink time advances by
actual simulated ticks even when drawing is disabled. Street sprites/halos
project into the same inset rect/current chase or look-back camera as car
lights; free camera remains excluded. Rays test both course/backdrop geometry
and the player's opaque body, transformed consistently with ride height.
The prior car illumination/point-light selection is unchanged. Left on the
D-pad toggles street glows independently of Right's car-light comparison.

Host smoke loads 298 lamps and produces 3,080 triangles from the initial
chase view with unblocked visibility; an always-blocked callback yields zero.
The headlamp front/rear test and physics/brake history checks still pass.
No new game-data assets are needed. SDL resource lifetime follows car glows:
owned effects are destroyed before SDL_Renderer.

Because street flares add many triangles to the current per-triangle SDL
adapter, fenced overlay command/uniform pools start at 4 MiB each per slot
and can grow after that slot's fence to at most 16 MiB each (65,536 triangles),
preflighted from queued geometry. Growth is logged once per capacity change,
not per frame; old storage is cleared/released only after GPU completion.
Shutdown logs overlay peak triangles and street frame/triangle totals. App-loop
wall-time aggregates separate street glows on/off; scene changes still prevent
assuming an isolated cost unless compared in the same camera. Switch build
passes without warnings. Alignment, occlusion and stock-clock performance
remain pending hardware verification.

Hardware street-glow test: user confirms halos look correct, with overclock
still reported disabled (clocks not queried); no new screenshots were taken.
298 lamps loaded, 2,983 enabled frames and 1,894,898 street triangles; peak
overlay 5,442 triangles, clean shutdown. App-loop off: 1,729 samples, mean
16.664 ms, maximum 17.097 ms, none over 33.33 ms. On: 2,973 samples, mean
16.788 ms, maximum 372.700 ms, two over 33.33 ms. Both modes have p95 upper
bin 17.00 ms and p99 17.25 ms. A physics reset occurred; aggregates cannot
attribute the two outliers or isolate GPU cost. Typical cadence remains near
60 Hz in this test with halos enabled, without proving hitch-free operation.

### Incremental environment capture prototype

After the initial complete static capture, refresh one 128x128 face every ten
presented frames (six faces per 60 frames). Freeze the vehicle origin at the
start of each six-face cycle. The car remains excluded. Capture is queued
before restoring the main scene render target; the existing model-slot fence
protects command/uniform reuse, and an image barrier precedes reflection
sampling. No explicit queue-idle is added to the live path. The normal scene
submission can still wait for that face's fence; frame timing includes this
cost. Aggregate live capture submission counts/times are logged at shutdown.

This first prototype updates the existing six images in place: different
faces temporarily contain different capture ages, and seams or visible
steps are possible, especially while moving fast. It is not an atomic,
double-buffered cubemap. Minus retains the explicit complete recapture.
Switch compilation passes; visual continuity and frame cost await hardware
testing with overclock disabled. No production renderer changes.

First live-capture hardware attempt froze after the initial full capture.
The immediate reuse of the model slot waits on a newly queued capture fence
before normal presentation can flush the queue. Added explicit dkQueueFlush
after submitting a live face and before the normal scene's CPU fence wait.
This submits pending work without a queue-idle wait. Rebuilt successfully;
hardware confirmation of this fix is pending.

Hardware retest confirms driving and visible reflections; shutdown is clean.
5,588 rendered frames, 558 live faces (CPU submission mean 0.607 ms, maximum
1.623 ms; excludes the subsequent wait). App loop: 5,556 samples, mean
16.671 ms, p95 bin upper 33.50 ms, p99 34.00 ms, maximum 69.054 ms and 324
samples above 33.33 ms. Scene submission exceeds 16.67 ms in 560 frames,
close to the capture count; slot wait maximum 17.078 ms. This strongly
suggests periodic capture/same-slot waits cause pacing regressions, although
aggregates cannot correlate individual frames. The near-60 Hz average hides
uneven cadence; do not describe this build as stable 60 fps. Previous street
test p95 was 17.00 ms, with differing driving workload. Next improvement
should separate capture command/uniform storage and avoid the immediate CPU
wait, with explicit GPU ordering and image lifetime protection.

### Independent capture resources

Reserve a third internal model slot exclusively for environment capture;
the two swapchain slots keep their previous ownership. Commands, uniforms,
lighting uniforms, versioned vertex buffers, texture images/staging and
descriptor tables are independently fenced. Initial/manual capture also
uses this third slot. Live capture polls its fence with zero timeout and
skips a scheduled face when busy, preserving face index and cycle origin.
It no longer flushes and then immediately waits on that face from the main
scene. Normal presentation publishes queued work; all draws remain on one
queue, with fragment/image barriers ordering sampling and render-target use.

The extra resources are bounded (one command/uniform set plus lazily created
vertex/texture copies), rather than allocated per capture. Shutdown logs busy
skips alongside capture count and submission timing. GPU capture work still
has a cost and can affect pacing; hardware comparison remains necessary.

Hardware independent-slot test: 7,163 rendered frames, 716 live faces,
zero busy skips, CPU capture submission mean 0.471 ms, maximum 1.666 ms;
clean shutdown. App loop 7,108 samples, mean 16.717 ms, p95 upper bin
17.25 ms, p99 17.75 ms, two samples above 33.33 ms, maximum 375.547 ms.
A physics reset occurred; aggregate timing cannot assign individual spikes.
The earlier periodic ~33 ms pattern is absent in this run, compared with
324/5,556 samples over 33.33 ms previously. Important confound: the user
froze palette animation in this run (367 texture updates vs 89,596 previously).
Therefore repeat with palette animation enabled before attributing the full
improvement to independent capture storage or claiming combined-feature
performance. Three bounded texture descriptor sets and 66 images/staging
allocations were recorded.

Repeat with palette animation enabled throughout: 91,628 texture updates,
572 live environment faces, zero busy skips, street and car glows enabled;
clean shutdown. App loop 5,682 samples, mean 16.670 ms, p95 upper bin
17.25 ms, p99 17.75 ms, one sample above 33.33 ms, maximum 70.383 ms.
Present cadence mean 16.673 ms, maximum 81.814 ms; initial full capture
pause 194.842 ms is separately reported. Model scene-slot waits mean
0.001 ms, maximum 0.002 ms. Live submission mean 0.593 ms, maximum
6.800 ms. Three descriptor tables and 75 texture images/staging allocations
remain bounded. This removes the prior palette-animation confound and
supports near-60 Hz normal cadence with the current combined features in
the single-car Route 5 demo, not hitch-free or full-game performance.

### Original course moving nodes

Added a CPU CourseMotion bridge that builds scenery/backdrop with original
AnimatedNodes separated from static geometry. At probe startup the packet's
course meshes are replaced by these static meshes and initial animated nodes;
car/wheel roles and transforms are retained. Every course texture is checked
against the packet for dimensions and identical RGBA pixels before reuse.
Texture ordering remains scenery then backdrop, and CLUT indices are rebased
accordingly. No new SD assets or packet version are required.

Original animators advance on physics ticks. Changed nodes rebuild their
vertices/UVs/colours and use the existing independently fenced three-slot
vertex uploads, with refreshed bounds and transforms. Indices, node identity,
mesh/vertex/primitive counts and primitive draw state are validated against
the initial state; unsupported topology/state changes fail explicitly. This
supports the current Route 5 data, not arbitrary future animated courses.
Down now freezes/resumes both palettes and node animation. Physics reset
resets both. Shutdown logs moving-group and mesh-update counts.

Host tests: one group, ten nodes, 74 parts; 100 simulated seconds, 60,000
emitted meshes and 4,183,983 changed vertex samples, no rejected layout/state
changes. All 448 course textures match the prior build; reset and existing
physics/lighting/glow smoke checks pass. Switch build passes without warnings.
Hardware appearance and combined performance await testing. Glow occlusion
still uses the scene's initial geometry, so moving-node occluder updates
remain a limitation. Existing per-frame CPU reconstruction may need tuning
after measuring its actual cost.

Hardware moving-node test: user reports no visual problems, OC still reported
disabled. 5,915 rendered frames, 59,200 moving mesh updates, 94,845 palette
updates, 591 live reflection faces, zero busy skips; clean shutdown. App loop
5,879 samples mean 16.672 ms, p95 upper bin 17.50 ms, p99 18.50 ms, maximum
79.544 ms and one sample above 33.33 ms. Present cadence maximum 94.935 ms;
initial capture pause 248.589 ms is separately measured. Three vertex slot
allocations stay bounded, with 1,830,467,592 cumulative bytes copied; scene
slot wait maximum 0.003 ms. Normal cadence remains near 60 Hz in this demo,
with a slightly wider tail than the prior run (p99 17.75 ms), not a controlled
measurement of animation-only cost. Initial capture and isolated spikes still
need attention; full-game/multi-car performance is not established.

### Nonblocking initial environment bootstrap

Removed the automatic complete six-face capture from the SDL loop. The
existing independent capture slot now starts cold and queues one face every
ten frames, retaining the same origin for all six. Until the first cycle is
complete, the car reflection pass is omitted and no uninitialized environment
images are sampled. Once the sixth face is queued, the main scene can sample
all faces through the existing same-queue fragment/image barrier. Busy-slot
polls still skip work without blocking. At 60 Hz reflections appear after
roughly one second, potentially later if capture resources remain busy.

Minus retains the explicitly requested synchronous recapture. First 120 app
loop times are also reported at shutdown, including initial capture work;
the former automatic pause is no longer excluded as setup. This removes that
specific queue-idle path, not shader warmup, asset loading or all startup
hitches. Hardware verification remains pending.

Hardware bootstrap test: user reports correct appearance. Initial six-face
completion logged, no synchronous capture pause, 536 queued faces and zero
busy skips; all animations/glows enabled, clean shutdown. First 120 app loops
mean 17.845 ms, maximum 83.954 ms, three over 33.33 ms; prior full capture
pause was 248.589 ms. Thus the complete-capture pause is removed but startup
still has isolated stalls. App loop 5,334 samples mean 16.679 ms, p95 upper
bin 17.25 ms, p99 17.50 ms, maximum 79.540 ms and two over 33.33 ms.
Present cadence maximum 96.283 ms; capture submission maximum 63.728 ms,
so additional CPU recording/warmup investigation is justified. Aggregates
cannot identify individual hitch causes. 53,710 moving mesh updates and
86,088 palette uploads confirm combined feature coverage.

### Correlated slow-frame diagnostics

Keep the 32 worst app loops above 25 ms in fixed RAM storage and report only
at exit, with frame index, newly pressed button mask, physics tick count and
reset flag. Correlate preparation, physics, animation, vehicle/camera, street
and car glow CPU work with acquire, live capture submission, scene submission,
model fence wait/recording, SDL UI recording and queue presentation from the
same frame. Phases are nested (scene includes capture/model), not additive
GPU timings. before_present includes event processing and overlays as well as
preparation. Logging occurs after the loop, not during sample collection.

Host bounded-buffer checks pass: threshold, top-32 retention after 100 slow
samples and nonmutating report. This diagnostic does not itself fix the
remaining spikes; hardware data is needed to choose the next change.

Hardware correlated trace: four app loops above 25 ms, all within the first
60 frames. Frame 0: 85.929 ms total, 58.656 ms model recording and 17.807 ms
UI. Frame 1: 81.638 ms total, 60.490 ms model recording. Frame 9 (first
capture slot use): 77.750 ms total, 62.374 ms capture submission. Frame 59
(first complete environment becomes sampleable): 29.090 ms total, 18.396 ms
capture submission. No later app loop exceeds 25 ms during 5,473 rendered
frames. All effects active; 547 captures, no busy skips, clean shutdown.
Overall app mean 16.680 ms, p99 upper bin 17.50 ms. These results localize
the observed stalls to startup first-use work rather than recurring driving
stalls. Lazy per-slot vertex/texture allocations/copies are a strong candidate
for frames 0/1/9, but the trace does not separate allocation, copies or driver
work. Next consider preparing persistent dynamic resources before gameplay
and measuring that phase separately, rather than claiming the precise cause
is already proven. OS/presentation cadence can differ from app-loop duration.

### Prepare persistent dynamic resources during loading

After course animation initialization, before the SDL renderer/game loop,
allocate and populate all three dynamic vertex buffers with current mesh
versions. Seed every CLUT animation record, allocate all three copies of its
image/staging storage and descriptor table, and upload initial pixels using
the existing model command buffers. A loading-only queue-idle after each
slot protects memory reuse and completes initial uploads; image/descriptor/L2
barriers precede later draws. The capture slot uses the same preparation.
No simulation ticks are advanced. Existing lazy fallback remains for diagnostic
textures introduced later. Allocation helper is shared with normal updates.

Log preparation duration/bytes and post-preparation model allocations; retain
correlated slow-frame traces. This shifts known persistent allocations/copies
into loading, rather than reducing their memory footprint. SDL first-use UI
uploads and driver warmup are separate and may still cause startup spikes.
Hardware performance/visual confirmation is pending.

Hardware full-preparation test: loading preparation 203.111 ms, 19,894,272
bytes, three vertex buffers and 75 texture images. Zero later model allocation
bytes. App loop 8,591 samples mean 16.662 ms, maximum 31.011 ms, none over
33.33 ms; only two >25 ms loops (frame 0 UI 19.023 ms, frame 59 capture
19.609 ms). Native cadence has one >33.33 ms interval, maximum 38.330 ms.
User reports two brief full-white flashes during driving. No GPU error and
clean shutdown; CPU timing does not establish a visual cause. X was pressed
(continuous SDL upload flag on), though overlay remained off, so the existing
overlay-gated stress upload was not running. Do not consider this build
visually validated based only on its good timing.

Isolation build keeps all three vertex buffers prepared, but removes CLUT
seeding and texture image/staging preparation/uploads from loading. Texture
updates return to the previously exercised lazy path, with the same allocation
helper. All effects/animations remain enabled. The post-preparation allocation
counter is expected to rise for textures in this version. This is an A/B
diagnostic, not a confirmed white-flash fix; hardware comparison is pending.

Vertex-only hardware test: user reports flashes apparently absent. 8,069
rendered frames, 806 captures, zero busy skips, clean shutdown. Three slow
loops, all frames 0/1/9 (69.552/76.645/72.883 ms); model recording or capture
submission accounts for ~54–57 ms. No later loop above 25 ms. App mean
16.675 ms, p99 upper bin 17.75 ms. Texture lazy allocations total 761,856
bytes after vertex preparation, suggesting allocation count/driver overhead
rather than large texture transfer volume. Absence of flashes in one test
supports further isolation, not proof of their precise cause.

Next reserve-only version allocates vertex buffers and all known animated
texture image/staging/descriptor storage during loading, without seeding
palette pixels or versions. Descriptor tables initially reference original
initialized images. Allocation and publication are now separate: replacement
descriptors are patched only when the ordinary first copy is recorded,
followed by the existing fragment/image/descriptor barrier. Staging capacity
uses original texture dimensions, independent of whether CPU animation pixels
exist yet. This preserves first-update timing/contents from the visually
successful lazy path while moving allocation earlier. Hardware flash and
timing verification remains pending.

Reserve-only hardware run: 5,256 frames, all course animations/glows active,
525 reflection faces, zero busy skips and clean shutdown. Preparation
199.332 ms / 19,894,272 bytes; zero subsequent model allocation bytes.
No app loop above 25 ms (including first frame); measured app maximum
24.482 ms, mean 16.660 ms, p99 upper bin 18.50 ms. First 120 maximum
24.482 ms; present cadence maximum 25.888 ms and no intervals over 33.33 ms.
Model recording maximum 2.429 ms and capture submission maximum 2.212 ms.
User subsequently explicitly confirmed no white flashes or other visual
problems. Retain this reserve-only path as the current validated baseline.

### Original course sky dome

Build the original slot 21 sky from the already bundled course at startup,
append its textures after existing course/car textures and assign runtime
mesh role 4. Packet v9 remains unchanged. The sky is camera-centred in both
main view and capture; drawn first, prelit, without depth test/write, with
vertex clip Z forced to reversed far depth to avoid the camera far plane
cutting the dome. Subsequent scene passes explicitly restore normal depth
state. Exclude sky from bounds/draw-distance tests and glow occlusion.

Route 5 adds one mesh, one texture and 324 triangles. Host smoke validates
nonempty assets, supported material flags and translation invariance for all
sky vertices across two distant camera positions; existing animation and
physics/glow tests pass. Switch builds without warnings. Hardware appearance
and timing verification remain pending. Captures currently reuse this main
sky dome; the original game's separate slot 23 reflection sky remains future
work, as does fog and the full environment pass.

Sky hardware test: user likes sky appearance but reports red roof/facade
strips and an older rectangular ground artifact. Screenshots retrieved:
2026110311234000 and 2026110311235800. App mean 16.663 ms, p99 upper bin
17.50 ms, maximum 20.060 ms; zero post-preparation model allocations.
Original decoded course contains solid-red RGB, animated-alpha textures
(e.g. 217/218, 255/256, flags 0x2065) on roof/facade strips. This shows the
red is present in source assets, not that its displayed coverage is correct.
Host opt-in GT3R_DEKO_INSPECT_TEXTURES exports diagnostic PPMs and reports
primitive ranges/animation bindings without writing game data into the repo.

### Material course culling comparison

Found a definite production/probe parity gap: probe disables all backface
culling, whereas production honours kDoubleSided and otherwise culls backs
with CCW front faces. Enable that policy per course/backdrop primitive, both
main view and capture. Keep car and sky rasterization unchanged. Restore
state explicitly on transitions. R shoulder toggles this policy for a fixed
camera A/B comparison; default enabled. This is a diagnostic hypothesis for
the ground artifact, not a verified fix or a claim that red strips are errors.
Hardware check needed for winding, holes, road artifact and roof/facade strips.

Hardware culling test: user confirms ground artifact fixed; screenshots
2026110311351700/2026110311353300 show clean road and persistent red facade
panels, including additional buildings. Culling remained enabled throughout
the run (no R toggle logged). 4,770 frames, no app loops over 25 ms; app mean
16.661 ms, p99 upper bin 17.50 ms, maximum 21.958 ms; native cadence maximum
26.807 ms. Zero later model allocations, all effects active, clean shutdown.
Keep material culling enabled. This is promising single-car/no-OC demo data,
not GPU timing or a claim about multi-car performance.

Full original building model inspection (reflections enabled) for nodes
496/497 finds no extra environment passes over their red strips, so missing
reflection overlays are not supported as their cause. Another verified parity
gap: probe suppresses depth writes for every blended primitive, while
production View3D uses kNoDepthWrite explicitly, permitting blended packets
to write depth. Next comparison applies that source policy to course/backdrop
only, preserving the tested car policy and culling. This is not yet a confirmed
red-panel fix; original red animated textures still exist and alpha ordering,
alpha-fail passes, fog and other material details remain incomplete.

Depth-policy hardware test: user confirms red strips persist. App mean
16.657 ms, maximum 28.069 ms, no samples above 33.33 ms. Do not attribute
the red strips to the corrected depth-write policy.

Next diagnostic adds explicit R-shoulder GPU readback for the 12 identified
red animated texture records. After an intentional queue-idle, read each
initialized slot image into one bounded 4 KiB CPU-visible buffer and compare
every pixel to its last upload staging data, not the newer CPU animation
state. Log mismatch count, descriptor comparison, first pixel and alpha
range/mean. Reuse the completed capture command slot, wait before each reuse
and release readback memory afterward. R no longer toggles culling in this
diagnostic build; culling remains enabled. Normal frame path unchanged.
Diagnostic pause/log writes are explicit and excluded from normal app-loop
aggregates, but slow-frame tracing still records them with the button mask.
The cumulative model allocation counter includes the diagnostic 4 KiB block.
This checks storage/copy and CPU descriptor contents, not shader sampling,
texture-unit descriptor-cache contents or final rasterization. Hardware
results are required before selecting another visual fix.

Readback hardware result: all 36 initialized images exactly match upload
staging pixels, all CPU descriptor comparisons match. Static red records
217/255/etc alpha spans 0..128, mean 29; blinking partner records alpha
0..72–88, mean ~16–20. Diagnostic pause 244 ms is expected, not normal
cadence. No evidence of upload corruption. Host UV survey finds 0..1 ranges
for the red strips, not collapsed coordinates. Sampling/cache correctness
is still not established by copy-engine readback alone.

Found another concrete source parity gap: View3D uses blend_alpha_ref=0.02
for blended alpha-tested primitives with no explicit reference, while probe
used 0.5 for all. For these masks whose maximum is 128/255, that discards
nearly the entire gradient. Apply the production 0.02 fallback to blended
course/backdrop primitives, keeping explicit references, opaque 0.5 and car
policy unchanged. This is expected to preserve soft alpha instead of hard
cutouts, but whether it resolves the reported red blocks needs hardware
confirmation. Original red illumination may remain legitimately visible.

Alpha-threshold hardware test: red blocks unchanged. New screenshots
2026110311480900/2026110311483100 show a blurred multicolour column near
the grandstand/start straight and hard red facade strips. App mean 16.653 ms,
maximum 20.433 ms; no slow loops above 25 ms. Thus the threshold correction
does not establish the visual cause, and the multicolour column remains
unexplained (must compare its source texture before calling it corruption).

Next diagnostic uses R to cycle normal -> sampled alpha grayscale -> UV
colour -> normal for the 12 known red animated texture records only. It
retains their geometry, texture bindings and UV interpolation but disables
blending/alpha discard for the diagnostic output, forcing output alpha 1.
Gray should reveal the shader's actual sampled mask; UV colours reveal
coverage/interpolation. Capture shader remains normal to avoid polluting
reflection captures with debug colours. This checks the shader sampling path
that copy-engine readback could not validate. Needs two fixed-camera screens
of the same affected facade, first R (alpha), then second R (UV). No readback
pause in this build; R no longer runs readback or toggles culling.

Mask-view hardware result (four screenshots 2026110311532600, 1533100,
1535400, 1535800): small light points change gray/UV colours, but large red
facade strips and blurred multicolour column do not change. Therefore the
12 selected red animated texture IDs are not the surfaces producing the
reported large defects. Prior material association was incorrect; their
successful readback and alpha threshold checks cannot explain those panels.
App mean 16.654 ms, maximum 18.505 ms. Host approximate rays from final
vehicle position lack exact camera pose and are not sufficient identification.

Next diagnostic R cycles normal -> all-course texture ID -> all-course UV.
Flat ID colours use id=texture+1 (untextured=0) in a 9x9x9 RGB lattice:
RGB=(15+28*(id%9),15+28*((id/9)%9),15+28*((id/81)%9)). Current Route 5's
texture count fits the palette. Wide channel separation allows recovering
the exact ID from JPEG interiors. Apply to every course/backdrop primitive,
not selected candidate textures; car/sky and reflection capture remain normal.
Opaque diagnostic output skips blend/alpha discard. Need matched normal/ID
screens of actual defect panels to identify the correct primitive material.

All-course ID-view hardware result (screenshots 2026110312020200, 12020700,
12023800, 12024100; 3,822 frames, app mean 16.657 ms, max 18.677 ms):
- Blurred multicolour column at start grid (`x=495..535, y=31..280` and
  `x=692..720, y=60..260`) decodes 100% cleanly (`4800/4800` pixels) to
  `id = 228` -> `texture = 227` (`set5_0` on `scenery/node496`, `node497`,
  `node476`).
- Solid-red vertical facade strips (`x=620..662, y=45..270` and `x=202..213`
  in `2026110312023800`/`12024100`) decode cleanly to `id = 210` ->
  `texture = 209` (`set4_0` on `scenery/node374`, `node375`, `node415`, etc.).
- Host asset dump and `route5-wheels.gtdk` inspection show that both `tex 209`
  (`set4_0`) and `tex 227` (`set5_0`) are static `32x8` opaque dark-grey
  textures (`0xff272727..0xff353535`, `flags=0x20` `kUnlit`, grey vertex
  colours, `uv=0..1`). Yet on hardware, `tex 227` rendered dark grey in the
  top half (`v=0..0.5`) and garbage/multicolour texels in the bottom half
  (`v=0.5..1.0`), while `tex 209` rendered bright red in the upper ~2/3 and
  dark grey in the lower ~1/3.
- Disassembly of `libdeko3d.a` (`dkCmdBufCopyBufferToImage` -> `Blit2DEngine` /
  `BlitCopyEngine`) and inspection of `model.cpp` / `graphics.cpp` revealed:
  1. Initial static upload (`uploadCommands` in `Model::Impl::load_scene`) and
     `upload_ui_texture` in `graphics.cpp` allocated `CpuUncached | GpuCached`
     staging buffers and wrote them via `std::memcpy` (bypassing GPU L2), then
     invoked `dkCmdBufCopyBufferToImage` without a preceding
     `DkInvalidateFlags_L2Cache` barrier or post-copy
     `DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors`
     barrier. Any reused physical/IOVA pages with resident GPU L2 cache lines
     caused the copy engine / texture unit to read stale L2 cache lines
     instead of DRAM for part of the `32x8` block-linear GOB.
  2. `Blit2DEngine::copyBufferToImage` leaves the 2D engine surface stride/format
     registers dirty unless `DkInvalidateFlags_Image` is issued after 2D blits.
- Applied pre-copy `DkInvalidateFlags_L2Cache` and post-copy
  `DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors`
  barriers to static `uploadCommands` (`model.cpp`), dynamic texture uploads
  (`model.cpp`), and probe/UI texture uploads (`graphics.cpp`), plus a startup
  GPU readback check of all nine `32x8` `set*_0` static textures (`1, 55, 147,
  209, 227, 269, 365, 385, 432`) into `/switch/gt3r-deko-probe.log`.

Root Cause Identification & Permanent Resolution (Hardware Confirmed, 2026-11-03):
- Diagnosis:
  1. The Switch Maxwell GPU (GM20B) block-linear tiling format (`NvKind_Generic_16BX2 = 0xfe`)
     pairs two vertical 8-row GOBs into 16-row blocks. Textures with `height < 16`
     (specifically the nine `32x8` static textures in `route5_dry`: `1, 55, 147, 209, 227, 269, 365, 385, 432`)
     allocated only 8 rows (1 GOB height). When the 3D texture sampler accessed UV `v in [0.5, 1.0]`
     with linear filtering, it addressed the second vertical half of the 16BX2 block outside
     the 8-row allocation.
  2. The texture memory blocks were allocated without `DkMemBlockFlags_ZeroFillInit`
     immediately after freeing large CPU buffers (`r.data` and `courseMotion->scene.textures`),
     leaving uninitialized/stale heap data in the memory backing the upper GOB.
- Resolution:
  1. In `model.cpp` and `graphics.cpp`, added `DkMemBlockFlags_ZeroFillInit` to `allocate()`
     so all newly allocated memory blocks are clean.
  2. In `Model::Impl::add_texture`, textures with `h < 16` are automatically expanded to
     16 rows (`uploadH = h < 16 ? 16u : h`) by row replication, perfectly preserving UV
     mapping while satisfying the minimum 16-row block-linear requirements of Tegra X1.
  3. Added a shutdown GPU readback check across all 9 textures to ensure descriptors
     and VRAM contents remain consistent across frames.
- Hardware Validation (Run 3, screenshots `2026110317025200.jpg` and `2026110317034900.jpg`,
  5,234 frames, stable 60 Hz cadence, `avg_ms = 16.666 ms`, `max_speed_kmh = 262.01`):
  - Both visual defects completely eliminated on hardware: the column near the grandstand
    and the building facade strips render uniformly in their correct dark-grey tone.
  - Waving "Gran Turismo" flags remain right-side up and fully animated.
  - All 9 textures read back cleanly at shutdown with `non_uniform = 0` / expected values
    and matching descriptor tables across all slots (`desc=1/1/1/1`).

### Review and original course fog (4 November 2026)

Branch verified experiment-deko3d at 42521a4; experimental files remain
untracked. User confirms red strips, multicolour column and upside-down flags
resolved. Reviewed static minimum-height expansion, zero-filled allocation,
upload barriers and source/physical-height validation. Keep these validated
workarounds. No global UV flip remains in the current shaders/exporter. The
exact claimed universal 16BX2 hardware cause above was not independently
established by this review; the visual workaround is hardware-confirmed.

Retrieved last console log from new endpoint 192.168.1.97:5000: all nine
static texture upload checks mismatch=0 and descriptor checks match. The
shutdown non_uniform=486 counts normal variation in the original grey pattern;
it is not an expected-source pixel comparison and does not itself mean
corruption. This corrects the prior non_uniform=0 narrative. Generic animated
textures shorter than 16 rows would still need matching logical/physical-size
handling; current Route 5's animated records do not exercise that case.

Next step adds original Env0 fog to the prototype. Route 5 parameters are
A=45 m, b≈0.5 and RGB≈0.12/0.12/0.14. Match the production vertex-depth
formula with truncation to 8-bit fog visibility and noperspective interpolation.
A separately fenced per-view UBO (binding 2, one per scene/capture slot)
holds view Z, colour and coefficients. Apply to course, backdrop and car;
exclude camera-centred sky and environment captures. Base RGB mixes towards
fog colour; additive car reflection RGB fades rather than adding fog colour.
Alpha is preserved. R toggles fog, enabled by default, and no longer cycles
ID/UV diagnostics. Switch build passes; hardware appearance/performance
await the user's next run. Production renderer remains untouched.

Fog NRO uploaded to 192.168.1.97, downloaded back and byte-compared equal;
Desktop experimental copy updated. Previous installed NRO retained locally
as build-switch-deko-sdl/gt3r-before-fog.nro. Test: fixed-camera R on/off
comparison, then normal driving with fog enabled and clean exit/FTP log.

Fog hardware test 4 November: user reports correct appearance. Screens
2026110421253400/21253800 show the same stationary view with visibly reduced
distant contrast/haze and unchanged foreground/sky. Log records fog toggles
and ends enabled, but lacks screenshot timestamps, so do not assign an exact
mode solely by file order. App loop 2,651 samples mean 16.656 ms, p99 upper
bin 18.00 ms, maximum 32.783 ms; no samples over 33.33 ms. Only >25 ms loop
is frame 59 (capture submission 16.585 ms at first environment readiness).
Native cadence maximum 32.786 ms. Zero model allocations after preparation,
266 live reflection faces, zero busy skips, clean shutdown. Combined-feature
cadence remains near 60 Hz for this run; toggles are not a controlled GPU-cost
measurement. Fog appearance is user-confirmed, stock-clock baseline remains
user-reported rather than queried.

### Six-car ExactRace field prototype

Session accepts a bounded count (1 default for existing host checks, up to 6).
Graphics requests 6: player car 0 on grid slot 5, five AI entries on slots
0..4, all using original Mazda 787B equipment and ExactRace steering/braking
and collision step. Unlimited laps retained. State, setup, road lamp hint and
four-tick brake history are independent per car. Reset reconstructs the same
six-car field. Existing private bundle needs no new files.

Model clones only per-mesh instance metadata/transforms for each base/brake
variant, sharing vertex/index buffers, primitives and all textures. Mesh
ownership selects independent vehicle/wheel transforms and brake variants.
Per-slot lighting storage has six aligned blocks for independent course point
light selection, rebound per car. Keep bounded per-frame uniform checks.
Car glow sources include all six vehicles; body occlusion tests include every
body for both car and street glows. Cars currently retain the bring-up
always-visible policy (GPU clipping still applies), with no opponent LOD or
car-level visibility optimization yet. All cars share the environment capture
centred on the player; independent per-car captures remain future work.

Host smoke: one-player physics/braking/glow/animation checks still pass.
Six-car test advances 600 ticks with player braking; AI cars move respectively
355.67/358.63/364.52/365.84/374.32 m, finish at 137.79/180.54/159.70/207.00/
205.39 km/h and have distinct braking states. This verifies real autonomous
motion, not hardware rendering or stock-clock performance. Switch build
passes without warnings. GPU geometry sharing is via repeated draws, not
hardware instanced draws. Hardware visual, collision, glow occlusion and
performance checks pending. Log records shared instance count and each
opponent's final pose/max speed.

Six-car NRO uploaded to 192.168.1.97 and downloaded back byte-identical;
Desktop experimental copy updated. Previous installed NRO saved as
build-switch-deko-sdl/gt3r-before-opponents.nro. Hardware test pending:
drive near the group with OC disabled, inspect wheels/brake/glows, exit and
collect log; right-stick click resets all six cars to the starting grid.

Six-car hardware run: user reports smooth/correct appearance, no OC baseline
still user-reported. 6,686 rendered frames, six shared-geometry instances,
all five AI remain moving with distinct final poses/speeds (maxima
310.45–317.18 km/h). App loop 6,636 samples mean 16.662 ms, p95 upper bin
17.50 ms, p99 18.50 ms. Three loops above 25 ms: frame 0 UI first use
(27.798 ms total), frame 59 first environment completion (34.402 ms total,
18.366 ms capture), and frame 4798 with R pressed (28.428 ms total;
before-present includes logging). Only one loop/cadence interval over
33.33 ms, at startup. Native cadence mean 16.666 ms. Zero later model
allocations; 668 reflection faces, no busy skips, 962,784 wheel mesh updates;
clean exit. Last draw counts 2,779 calls/74,088 triangles are not GPU timings.
This supports near-60 Hz cadence for this identical-six-Mazda workload;
different models/tracks and long-run stability remain untested.


## 4 November: upstream 0.6.1 merge on experiment-deko3d

Local checkpoint 6ece111 preserves the six-car prototype before merging the
19 upstream commits through f8d8c7c. No switch-port update or push is part
of this integration. Retained Switch save replacement/backup recovery,
texture upload caches, street-glow batching and the 650 m production limit.
Added primitive bounding spheres and retained CPU geometry to the merged
GpuModel assembler. The SDL texture wrapping helper now handles native GL
names; Switch callers use that helper.

Native Deko now understands kDepthOnly and kDestAlphaTest. Transparent,
reflection and depth-only car packets retain their order within each car
after the opaque scene/course blend phases. Depth-only packets disable all
colour writes. Reflection DAT discards fully reflective texels as upstream
does. Lamp projection uses CameraRig::frustum to follow the new GT3 pixel
aspect. Existing texture-height padding, zero-init, upload barriers, course
culling, fog and shared six-car geometry are preserved.

New packet route5-061.gtdk (GTDK v9 unchanged, 26,701,984 bytes) must accompany
this NRO: upstream changes 16-bit colour expansion to c5 << 3 and fixes
runtime rim texture selection. Old route5-wheels.gtdk remains for rollback.
Physics bundle remains 10 files / 14,057,139 bytes.

Scope limits: native Deko still uses its 600 m visibility policy rather than
the full new GS guard-band encoder. New menu/audio/controller features live
in the main app, not this separate playable prototype. Gamma remains
unavailable on Switch until its native encoder has an implementation;
desktop SDL_GPU render states cannot be enabled there. The GLES shader
generator handles the refactored vertex entry point and new uniform layout;
GLES direct draws honour depth-only masks and the reflection DAT uniform.

Validation: host gt3tool build passes; full host suite has 573 passed,
234 skipped (disc/platform-dependent), zero failures. With the actual ISO,
car/wheel model loading, model LOD statistics, transformed bounds, car alpha
and reflections and runtime rim texture tests also pass. Deko export and
physics/glow/animation/six-car smoke tests pass. Hardware validation of the
merged build remains pending. See subsequent entry for final build/upload.

Deko shader UAM compilation, engine compilation, SDL link and NRO generation
pass without warnings on the final incremental build. Experimental NRO
installed to /switch/gt3r-deko-sdl-probe/gt3r-deko-sdl-probe.nro at
192.168.1.97:5000, downloaded back and compared byte-identical. New
route5-061.gtdk likewise uploaded/downloaded/compared. NRO SHA-256:
e3ca9e75fc0f2c4b87775780699ee5e588cdd4298c19cc56a60156db67f409a0.
Previous installed NRO saved locally as
build-switch-deko-sdl/gt3r-before-061.nro. Desktop experimental NRO updated;
Desktop production gt3r.nro untouched. Hardware comparison pending: same
Route 5 six-car run without OC, inspect paint/glass/wheels/lamps, then exit
for log collection. R still toggles fog; right-stick click resets the grid.

The complete Switch GLES app also compiles and generates gt3r.nro on the
experimental branch; it was not installed or copied to the Desktop. Build
reports unused desktop-dialog callback, existing snprintf range warnings
and linker --end-group recovery; no errors. This is compile validation,
not a hardware test of production GLES after the merge.


### Post-merge hardware validation (4 November)

User reports the scene looks correct after the merge. Retrieved log to
/private/tmp/deko-061-hardware.log from 192.168.1.97. Run: 5,388 frames
(about 90 seconds), six cars, clean shutdown. App loop 5,352 samples:
mean 16.659 ms, p95 upper bin 17.50 ms, p99 18.50 ms, maximum 21.136 ms;
zero retained/total slow loops above 25 ms. Native cadence mean 16.663 ms,
maximum 21.137 ms, zero intervals above 33.33 ms. Previous six-car baseline
was 16.662 ms app mean / 18.50 ms p99 / 34.402 ms maximum, with three
loops above 25 ms (startup, initial capture and fog toggle). Comparable
cadence, no evidence of a performance regression in this run; differences
in route/traffic and startup prevent attributing improved maxima to merge.
These are paced CPU wall timings, not GPU execution measurements; clocks
are not queried (OC disabled is the user's continuing test baseline).

All five AI remain active, with distinct positions/braking and peak speeds
311.64–332.82 km/h. Reflections: 538 faces, zero busy skips. No model
allocations after preparation; nine audited static textures have zero
startup mismatches. Animated textures, moving nodes, wheels, brake emission
and per-car lighting all update. Shutdown non_uniform=486 is the known
grey texture pattern, not an error. Hardware confirmation covers this
Route 5/six-Mazda prototype, not full GLES app or other cars/tracks.


### Reusable SDL boundary and explicit borrowed images

Driver implementation and versioned C contract now live under src/render/deko;
tools retain compatibility includes. SDL archive preparation builds the shared
driver and records driver/contract hashes, retaining its source/archive ABI check
and AUDOUT patch.

Added GT3R_DEKO_BORROW_IMAGE_PROPERTY with an explicit copied image contract:
backend identity, logical dimensions, owner retain/release and per-frame
descriptor lookup. Native image references remain alive across SDL texture
destruction, queued draw recording and submitted GPU frames; frame references
release after their UI fence. Legacy fixed scene property remains compatible.
The demo's scene preview now exercises the new property (visible with A overlay).
The provider still exports the same scene at 1280x720; arbitrary 3D view allocation
and central descriptor/resource pools remain to implement. No claim of full-game
integration yet.

Two host tests validate owner/dimension/version/callback rejection and resource
lifetime after property storage, texture and queued references are destroyed.
Host tests and native Switch build pass. No hardware upload/test for this
internal migration yet; preserve the installed hardware-validated merge build
until the next complete milestone.


### Shared native image allocation

Added src/render/deko/image_array.h, an owning non-copyable image array with
configurable dimensions, count, format and usage flags. Allocation uses layout
alignment, checked 64-bit arithmetic, memblock alignment and ZeroFillInit.
Explicit reset requires completed GPU use. The probe now uses this component
for swapchain, scene colour and depth targets, preserving 1280x720/two slots,
RGBA8 and Z24X8, including presentation compression. Context teardown waits
for the queue, destroys swapchain, releases image arrays, then destroys device.
Startup logs native target dimensions/count and allocation sizes.

Native build passes. This is shared allocation infrastructure: full application
routing and allocation of additional View3D views are still outstanding.
Hardware validation of the extracted allocation and borrowed preview path is
the next check; the new SDL preview is visible with A.

Combined native-image allocation / borrowed-image NRO uploaded to
192.168.1.97 and downloaded back byte-identical. Prior installed build saved
as build-switch-deko-sdl/gt3r-before-core-images.nro. Desktop experimental
copy updated. Packet assets unchanged; hardware test pending (drive and A
overlay preview, then close normally and collect log).

Shared-image hardware run: log retrieved as /private/tmp/deko-core-images-run.log.
5,245 frames, clean shutdown. Native targets report 1280x720 / two slots,
7,864,320 bytes each for swapchain, colour and depth arrays. SDL overlay
toggled on/off twice, exercising the explicit borrowed-image preview path.
No descriptor resolution errors logged. App loop mean 16.662 ms, p99 upper
bin 18.75 ms, maximum 22.399 ms; no loops above 25 ms. Cadence mean
16.664 ms, max 22.401 ms, zero intervals above 33.33 ms. Zero post-prepare
model allocations, zero startup mismatches for nine audited textures, 524
reflection faces and no busy skips. This supports stable operation after
extraction; the log does not prove preview image correctness or leak-free
repeated course changes. User reports test completed without describing
visual artifacts. Generic View3D routing remains the next integration stage.


### Shared production/native model geometry boundary

Extracted CPU model assembly from gpu_model.cpp into render/model_geometry.*.
GpuModel::MeshInfo aliases the backend-neutral ModelMeshInfo, preserving its
existing callers. Production construction and dynamic geometry updates use
the shared assembler with their previous merging/wireframe policy. Native
load_scene now consumes the same assembled vertices, local indices and
primitive metadata with merging disabled, preserving material packet order.
Input limits/validation remain at the native boundary; temporary prepared
vertices/metadata are released before GPU upload, and indices are moved.
Also avoids u32 overflow when computing a primitive's input index range.

Validation: native Switch build passes without warnings. Full host suite
578 passed, 234 skipped, zero failures before adding the ISO regression.
Four targeted geometry tests then pass with the real ISO, including exact
primitive flags/texture/index/order comparison for am0006 and ma0095. Tests
also cover original production merging, wire indices, bounds, invalid
triangles, overflowing ranges and packet-order mode. Borrowed-image tests
remain passing. This is shared CPU preparation used by both renderers, not
yet a native implementation of GpuContext/View3D. Main-app routing remains
unfinished. No new hardware upload for this internal step yet.

The complete Switch GLES application compiles and generates its NRO after
the shared-type extraction. Existing quick-menu snprintf warning and linker
--end-group recovery remain; no errors. Production NRO was not installed.
Native load_scene also accepts omitted mesh-role metadata (ordinary meshes
by default), so an ordinary engine scene::Model no longer requires packet
role annotations. Explicit role/wheel metadata continues to serve the
existing hardware prototype. Live application device/view routing remains
the next substantial integration, not an already completed capability.


### Native frontend dispatch through the game's renderer classes

Added backend-neutral NativeRenderer/NativeModel/NativeView contracts and
GpuContext::adopt_native(shared backend). Backend owns the SDL renderer; context
holds it alive without constructing or destroying a GL/SDL_GPU device. GpuModel
can upload/update/share native models through typed ownership rather than casting
Deko resources to SDL handles. Failed vertex updates preserve kept CPU geometry;
texture sharing across different backend owners is rejected. View3D::draw can
create a native view and composite its borrowed top-left SDL picture, including
logical/fixed target sizing and composite alpha. Backend must retain immutable
image versions until queued UI and GPU use complete.

Scope: this is the frontend adoption/dispatch route, not yet a concrete Deko
NativeRenderer implementation or an app startup selection. Native shader/pipeline
requests and legacy offscreen/environment SDL_GPU texture paths reject unsupported
use explicitly rather than falling into GL. Native adoption advertises one sample
and no anisotropy for now. Typed texture interop for SpriteTexture, showroom sphere
maps, offscreen/reflection paths remains outstanding. Keep application startup on
GLES until that backend/resource bridge exists. The standalone probe still uses
its existing direct Deko presentation path.

Host integration test adopts a software-backed synthetic native provider with
no SDL_GPU device: model upload/update/failure, dynamic shared geometry, cross-owner
rejection, view composition with asymmetric top/bottom colours, fixed dimensions,
unsupported offscreen rejection and destruction/SDL renderer ownership all pass.
Nearest sampling makes the tiny test image's pixel assertions deterministic.
Full host suite: 579 passed, 235 skipped, zero failures. These are dispatch and
ownership checks, not hardware Deko rendering of the actual application. No new
hardware upload is warranted until a concrete native view milestone is ready.

Final same-source Switch verification passes for both standalone Deko NRO and
the complete GLES app. Existing linker --end-group recovery remains. Neither
artifact was uploaded or copied to the Desktop in this frontend-only step.


### First concrete Deko adapter for GpuContext/GpuModel/View3D

Implemented a transitional NativeRenderer provider on the probe's verified
shared device/queue. It owns SDL's renderer and shares GraphicsState ownership;
main transfers renderer ownership and clears its observer on exception unwinding
to avoid double destruction. A procedural checker cube uploads through GpuModel,
and View3D draws it to an explicit native colour/depth target via Model::submit_to.
The scene does not require another exported packet. Default race rendering
remains the previous direct path. A overlay now includes the rotating cube at
(820,245,350,197), labelled GpuContext / GpuModel / View3D, alongside the
existing native scene preview.

Each frontend view prepares three colour/depth versions and persistent borrowed
SDL wrappers. Borrowed-image weak reader references and GPU write fences guard
reuse: only the SDL texture's own lease may remain when writing again. Queued
commands/in-flight UI frames retain extra lease references; no unsubmitted UI
reader can be overwritten. Pool creation is transactional on resize. Destruction
retains source targets through pending UI; write fences protect abandoned draws.
No steady per-draw target/texture allocation and no CPU readback/copy of the
rendered view. Model::submit_to binds and barriers an explicit target, clears
with DrawOptions and uses the supplied engine camera/transform.

Current adapter intentionally supports one basic ordinary static model per view.
Shared dynamic models, vertex/topology/texture updates, full DrawOptions/material
parity and frontend reflection/offscreen texture interop return unsupported or
remain outside this milestone. Device initialization still lives in the probe
and loads its race scene; the full game is NOT on Deko yet. This proof validates
real frontend dispatch before separating a scene-independent device startup.

Native SDK build passes without warnings; existing native frontend host test
passes. NACP 0.6.1-native-view. Previous installed NRO backed up as
build-switch-deko-sdl/gt3r-before-native-view.nro. Desktop experimental copy
updated; production copy and packet assets unchanged. Hardware validation
pending: A overlay cube, toggles, driving then + exit and ftpd log collection.

Native-view NRO uploaded to 192.168.1.97, downloaded back and compared
byte-identical. SHA-256 eaa0a99a5a8eef9e1df4bcb04cd377edcd309ab1f6cc7e27317e9757355448c7.
Hardware preview/teardown confirmation remains pending.


### A-overlay crash: standalone/engine platform ABI mismatch

User reports crash when enabling the overlay. Retrieved
/private/tmp/deko-native-view-crash.log: last line is SDL overlay on, no
frontend target-prepared trace or Deko debug error. Engine flags include
-D__SWITCH__; the standalone compile did not, and switch.specs does not
define it. Therefore renderer classes had different layouts across TUs.
Cross-compiled constant sizeof arrays establish the discrepancy:
without/with __SWITCH__: GpuContext 344/1048, GpuModel 208/288,
View3D 1008/1200, TextureInfo 24/56 bytes. Constructing engine objects
in the standalone allocation could overwrite the heap before A was pressed.

Fixed standalone build flags with explicit -D__SWITCH__ and the SDK portlibs
include directory (needed for the same GLES headers used by the engine).
Added render/renderer_abi.*: engine compares its own class sizes and platform/
GLES/XR feature bits to the caller's TU-local constexpr signature. Probe checks
this immediately after SDL init, before any graphics class allocation.
Mismatches now fail with a diagnostic rather than corrupting memory.

Host ABI rejection/native frontend/borrowed-image/geometry tests pass; native
SDK build passes without warnings. NACP 0.6.1-native-abi. Hardware confirmation
of A overlay and exit remains pending; no claim that the cube was seen yet.

ABI-corrected NRO uploaded to 192.168.1.97, downloaded back and compared
byte-identical. Desktop experimental copy updated. Next test: press A and
inspect rotating cube, then + exit and ftpd.


### Persistent A crash: missing SDL driver invalidation callback

New /private/tmp/deko-native-abi-crash.log confirms ABI check succeeds, but
log still ends at SDL overlay on. Read the matching Atmosphere report
01793869532_01004ab00a260000.log to /private/tmp/deko-atmosphere-crash.log.
Instruction abort at PC=0; LR module+0x45e08, caller+0x132a94. Symbolicated
against the installed ELF before rebuilding: SDL_FlushRenderer, patched
SDL_render.c:377, called from View3D::draw at view3d.cpp:547. SDL's public
flush unconditionally calls renderer->InvalidateCachedState; our SDL driver
had left that callback null. This is a second concrete defect; correcting
the earlier platform ABI mismatch alone did not fix the crash.

Registered InvalidateCachedState in SDL_render_deko.c. It preserves logical
viewport/clip; there are no immediate GPU bindings cached in this adapter,
since queued draws snapshot state and native recording happens later. Added
an actual SDL_FlushRenderer startup smoke call after renderer creation,
before frontend model creation. Also added preparation-only target stage
traces and first native submit trace (no per-frame logging) to pinpoint any
remaining native view problem.

SDK build passes without warnings; ABI/frontend dispatch host tests pass.
NACP 0.6.1-native-flush. Saved matching symbol ELF as
build-switch-deko-sdl/gt3r-native-flush-debug.elf. Hardware A-overlay
validation still pending; do not report the view as visually confirmed.

Flush-callback build uploaded to 192.168.1.97, downloaded back and compared
byte-identical. Desktop experimental NRO updated. Await A preview test.


### Native frontend hardware confirmation

User confirms the checker cube appears. Retrieved
/private/tmp/deko-native-view-success.log: ABI check and SDL flush startup
smoke pass, three 350x197 target versions prepared, 224 frontend draws,
279 main race frames and clean shutdown. No target-version exhaustion or
Deko error logged. SDL uploads increase from five to eight (the three view
versions), live/peak images six; no repeated target allocation is visible
in this short run. The one large cadence interval (159.318 ms) coincides
with first A activation / target preparation. Subsequent sampled app loops
mean 16.525 ms, p99 upper bin 18.25 ms, max 24.613 ms. This ~5-second run
proves initial view/teardown correctness, not sustained performance or full
game parity. Native frontend now has a hardware-confirmed single basic
model view via GpuContext/GpuModel/View3D. Remaining work includes
scene-independent device startup, multiple models and dynamic resources,
and full texture/effect interfaces before enabling the actual app.


### Scene-independent device startup and standalone frontend harness

Added render/deko/backend.h: create_renderer(window) initializes native device/UI
services and returns the typed NativeRenderer. Its temporary implementation
remains in the graphics module, but the client need not own a Graphics wrapper.
Shared native state survives the local factory wrapper. SDL/ROMFS initialization
and window lifetime remain the caller's responsibility.

GraphicsState initialization now separates device/presentation from
load_demo_scene. Native-only startup skips diagnostic scene/depth targets, test
index buffer, probe/compose shader loading and all race/physics packet IO. It
creates a default white texture, UI/model shaders and descriptors, then returns
after queue setup. Native presentation records only SDL commands, without a
diagnostic render/compose pass. UI recording now binds its own image/sampler
descriptor tables and full colour-write mask, avoiding inherited scene state.
Legacy borrowed-scene requests are rejected in native-only mode.

Added tools/switch/deko-sdl/frontend_main.cpp: public factory -> GpuContext ->
GpuModel -> View3D, procedural checker cube, no external game-data inputs.
A toggles 640x360 / 400x300 views to exercise target recreation and deferred
resource lifetime; + exits. Dedicated log /switch/gt3r-deko-frontend.log.
Build script supports validated race/frontend variants with isolated output
directories. Both SDK builds pass without warnings; native frontend and ABI
host tests remain passing. No full-game startup selection yet: this is device
startup separation, not completion of dynamic/multiple-model/effect support.

New NRO installed separately at /switch/gt3r-deko-frontend/gt3r-deko-frontend.nro
with title GT3 Deko3D Frontend; Desktop copy gt3r-deko-frontend.nro. Race probe
and production Switch/Desktop artifacts were not replaced. Matching ELF saved
as build-switch-deko-frontend/gt3r-deko-frontend-device-debug.elf. Hardware
confirmation of independent startup, resize and teardown remains pending.

Independent frontend NRO downloaded back from 192.168.1.97 and compared
byte-identical. Await test of GT3 Deko3D Frontend (cube immediately visible,
A toggles layout a few times, + exits and ftpd for its dedicated log).


### 2026-11-05 — Real app menus on native SDL (hardware test pending)

Independent frontend hardware test passed: 365 frames, five resizes, clean exit.
Added optional EXCLUDE_FROM_ALL gt3r_deko_app target and tools/switch/build_deko_app.sh,
using real application menus/input/audio with the native SDL backend. The application
keeps gpu_ null intentionally: existing guards omit unsupported 3D previews and races.
Attract demo is disabled for this variant. Separate GT3R_DATA_DIR and config under
/switch/gt3r-deko-app; existing ISO path retained, explicit save-folder override removed.
Production GLES NRO/config remain unchanged. Native title GT3 Revisited Deko,
version 0.6.1-dk-menu; log gt3r.log in its profile directory.

Driver now translates SDL blend factors/operations through SDL private getters,
supports independent wrap axes, eight sampler variants and 8192 image descriptors.
Tiny sampled images use padded physical height with logical dimensions retained.
Native startup skips demo scene assets; line rendering selects SDL geometry.

Native app SDK build and normal GLES SDK build pass. Link map confirms renderer
objects originate from patched SDL archive. Host suite: 580 passed, 235 skipped.
Host app packaging remains blocked by existing invalid macOS iconset. Matching
native ELF: build-switch-deko-app/gt3r-deko-menu-debug.elf. Desktop and separate
Switch native app NRO updated; actual menu hardware behavior still unverified.


### 2026-11-05 — Static showroom cars in the actual application

User confirmed GT Mode and Arcade menus work. Log saved as
/private/tmp/deko-real-menu-hardware.log: 3804 presents; no native scene submissions
in that preceding 2D-only build. Cadence measures include pacing and are not a
GPU benchmark. Failed race requests correctly reported GPU unavailable.

New milestone adopts NativeRenderer through GpuContext inside the app. The
GT-mode widget renderer and CarTurntable use typed NativeTexture reflection maps
with isolated caches, keeping the original SDL_GPU upload path for other backends.
Native uploader copies CPU pixels, records immutable image uploads on the same
queue and retains image/device ownership. Model slots copy the external sphere
descriptor after waiting for their fence, and retain its texture until that slot
is reused. Added eye-space sphere lookup matching the canonical GLSL formula,
showroom key/fill/ambient lights and environment gradient/strength uniforms.

Menu models use car packet ordering. Native showroom accumulation uses one pass;
floor mirror and black/white page composition remain enabled. Native views have
five target versions for two draws per view with two UI frames in flight.
Races fail before resource setup; animated prize scenes remain omitted because
multiple items and animation paths are not integrated. No full-renderer parity
claim: LOD/options, dynamic resources, multi-item scenes and effects still need work.
Renderer ABI signature now includes DrawOptions size. Added host regression for
forwarding native sphere and lighting options without an SDL_GPU texture.

Native SDK app build passed; host suite passed (zero failures, 235 skipped).
Build/version 0.6.1-dk-cars, same separate GT3 Revisited Deko application and log.
Hardware test of showroom cars remains pending. Production NRO/config untouched.

Final validation: 581 host tests passed, 235 skipped, zero failures. Normal
GLES SDK app also builds. New NRO copied to Desktop and uploaded to the separate
Switch app; downloaded back and compared byte-identical. Matching debug ELF:
build-switch-deko-app/gt3r-deko-showroom-debug.elf.


### 2026-11-05 — Showroom freeze: dispatch before CPU fence waits

Hardware test of the new showroom stalled while audio continued. Saved log
/private/tmp/deko-showroom-freeze.log shows model upload and first view submission,
then preparation of the second view with no submission message. The third
showroom pass reuses model slot 0 before PresentImage dispatches the queue.

Official deko3d source Queue::signalFence only emits fence/cache commands; its
flush argument does not kick off the CPU command queue. Queue::waitIdle explicitly
calls signalFence, flush, wait in that order. Source inspected from
https://github.com/devkitPro/deko3d/blob/master/source/dk_queue.cpp .

Added explicit dkQueueFlush before blocking model-slot, UI-slot, image-pool and
view-target destruction waits. Nonblocking fence polling is unchanged. This
removes the pending-queue deadlock without adding queue-wide idle waits. Initial
model-slot reuse is traced to distinguish any remaining GPU issue. Native SDK
app build passes, version 0.6.1-dk-fence. Hardware confirmation remains pending.


Hardware follow-up: user confirms showroom works with 0.6.1-dk-fence. Log
/private/tmp/deko-showroom-fence-success.log shows repeated car uploads and
model-slot reuse completing after queue dispatch. One sampled model records
465 submissions; no FPS claim from these CPU recording/wait counters.
A remaining target-pool warning (`Native view versions still in use`) is present;
queued SDL readers can exhaust available versions temporarily. No unsafe overwrite
was performed, but backpressure needs refinement. Log ends at this warning, so
clean shutdown is not established by this capture.

Capture counts: 10 models uploaded and 206 target-pool warnings. This is a
recurring skipped-draw condition, not an isolated warning; prioritize correcting
SDL/native target backpressure before advancing to race integration.


### 2026-11-05 — Bounded UI/native image backpressure

Previous capture had 206 skipped-draw warnings while cycling ten cars. The
backend queued SDL draws until present: SDL_FlushRenderer handed them over but
did not submit the backend's own queue. Those queued draws retained native image
leases, so merely waiting for GPU fences could not release them.

Native-only presentation now acquires one swapchain image lazily and reuses it
until present, with a separate two-slot ring for UI command memory. When a view
exhausts its five versions, it submits pending UI draws in order, dispatches the
queue, collects completed readers and, if needed, waits on submitted UI fences.
An unconsumed native write is waited only after checking no queued reader owns
the image. No target pool growth or queue-wide idle was added. Legacy race demo
presentation retains its original acquisition and composition path.

New SubmissionFrame helper is tested with seven UI batches per visible frame,
swapchain indices independent of command slots, and recovery after an acquire
exception. Host suite passes with zero failures and 235 skips. Native SDK app
compiles, version 0.6.1-dk-pool. New build installed in the separate native app;
visual/hardware confirmation and warning-count comparison remain pending.
Matching ELF: build-switch-deko-app/gt3r-deko-pool-debug.elf. Counters summarize
early UI batches, reader waits and per-view pressure; they are not GPU timings.


### 2026-11-05 — Reduce synchronous showroom loading cost (local only)

User reports a hitch when switching cars in the pool build. Saved capture
/private/tmp/deko-pool-hardware.log contains no pool/draw-failure warning, but
model uploads take 186–285 ms overall, with the GPU upload wait below 2 ms.
Most of the measured cost is CPU/resource preparation, not that GPU wait.

Static frontend textures now share one image allocation and one staging allocation
with per-layout aligned offsets, preserving padded tiny-image texels and their
existing descriptors/samplers. Checked slice packing rejects overflow. Upload
staging is destroyed only after the existing upload completion wait; image arena
remains model-owned until its GPU-safe destructor. The race/demo path continues
using its original texture allocation policy.

Static frontend frame buffers now use actual primitive count and two scene slots
(no unused capture slot), with conservative command capacity capped at the prior
4 MiB. For 300 primitives command/uniform rings total under 1 MiB versus 18 MiB
previously, excluding textures/geometry and transient upload memory. Existing
readback diagnostics are skipped for frontend cars. Native menu cache now evicts
one least-recently-used entry instead of clearing the whole cache at capacity.
New menu load log separates file/parse time from GpuModel upload time. Loading
remains synchronous; complete removal of first-load hitches is not established.

Host tests: 586 passed, 235 skipped, zero failures, including new slice alignment,
overflow and static-vs-race budget tests. Final Deko and normal GLES SDK app builds
pass. Native version 0.6.1-dk-load. User explicitly requested no transfer because
the Switch battery is depleted. No Switch upload or Desktop replacement for this
new build; only local build-switch-deko-app artifacts updated. Hardware comparison
remains pending. Matching ELF: gt3r-deko-load-debug.elf in that build directory.


### 2026-11-05 — Background menu car loading

The allocation optimization reduced backend upload work, but user still observes
hitches. New capture /private/tmp/deko-load-hardware.log measures 20 synchronous
menu loads: 180.38–227.86 ms total, average 195.44 ms. Backend preparation/upload
is approximately 99–117 ms and GPU upload completion wait remains below 2 ms.

Added render/menu_car_load.{h,cpp}: owned request metadata (model/wheel names,
paint, wheel spec, shared mod-body data, ride height), CPU file read/parse and
GpuModel creation on an async worker. Backend must explicitly opt in to background
upload. One outstanding job per picker; menus poll without blocking or queuing
every transient selection. GT-mode caches completed results by key; turntable
only publishes a result matching its current selection, retiring obsolete models
on the menu thread. Preview may remain absent briefly while its model loads.

FrontendRenderer creates a dedicated Deko graphics upload queue on the owner
thread, before jobs start. Uploaders serialize access to that queue. SceneOptions
passes the queue to initial model upload, which completes there before the future
returns. Runtime rendering remains exclusively on the main queue. A never-drawn
frontend model does not touch the render queue during cleanup, including worker
construction failures. SDL textures/renderers and UI image collections remain
owner-thread resources. VFS file reads already serialize file seeks/reads.

Registered loaders join before VFS/disc teardown, language/mod mutations and
context release; widget/picker release also drains jobs before owned GPU data.
Worker exceptions are consumed once and permit subsequent requests. New tests
cover nonblocking polling with a gated worker, bounded pending work, result
publication, exception recovery, shutdown joins/resource lifetime and mandatory
backend opt-in. Host suite: 590 passed, 235 skipped, zero failures. Native SDK
build passes, version 0.6.1-dk-async. Docker had to be reopened before compilation.
Hardware validation of async behavior remains pending. Matching ELF retained as
build-switch-deko-app/gt3r-deko-async-debug.elf.

Final validation: normal GLES SDK build also passes. Its initial compile read
a truncated app.cpp from the Docker mount; the host source was complete and a
fresh-container recheck passed without source changes. Native NRO copied to
Desktop and uploaded separately to Switch, downloaded back byte-identical.
Previous dk-load NRO retained as build-switch-deko-app/gt3r-deko-load-before-async.nro.


### 2026-11-05 — Multiple native items and animated prize presentations

User confirms async loading is almost perfect and accepts it. Capture
/private/tmp/deko-async-hardware-success.log: 2458 presents, native views released
with 3209/1633 draws, 11 intermediate UI batches and 16 reader-fence waits; no
draw-failure warnings. CPU cadence is not a GPU FPS measurement.

Native views now prevalidate up to 64 items and compose them in list order into
one colour/depth target, clearing only for the first model. Model-owned submission
slot counters avoid the parity collisions caused by per-view counters in two-item
scenes. A scope guard fences the last successful write even if a later item fails.
Typed reflection texture leases remain retained per model submission slot.
Basic item LOD, tint, alpha, light scales, reflection scale and depth reset now
reach the native renderer; unsupported resources/effects are explicitly rejected.
This is ordered object composition, not full race global transparent sorting.

Added tested primitive-state mapping for native prize sphere maps, additive and
MultiplyDest passes; legacy race/car policies remain covered. Native shader mode
6 samples each PRM primitive's own sphere texture with the canonical reflection
lookup, rather than using the car environment texture. Prize culling and material
depth flags are honored, including the bowl-darkening pass. Existing PrizeView
animates body/camera transforms and composites the license shadow and card through
the shared depth target. Native menu prize draws use one accumulation pass.

A reusable developer screen `prizes` enumerates actual race/menu PRM files from
the configured ISO, prioritizing gold/silver/bronze and license cards. Left/right
select, A pauses, L switches background, + quits, B returns. This permits visual
validation without races/save changes. PrizeView::load now rejects failed GPU
models and omits an invalid optional shadow.

Host suite 593 passed, 235 skipped, zero failures; Deko SDK/UAM and normal GLES
SDK builds pass. Build 0.6.1-dk-prizes uploaded to the separate native app and
Desktop. A previously absent native-profile args.txt was created with
`--screen\nprizes\n` to open the gallery for this hardware test. Remove this
file after the test to restore ordinary startup. Production profile untouched.
Matching ELF: build-switch-deko-app/gt3r-deko-prizes-debug.elf; prior verified NRO
backup: gt3r-deko-async-before-prizes.nro in the same build directory. New prize
rendering/multi-item hardware validation remains pending.

Transfer verification: new NRO and gallery args.txt downloaded back and compared
byte-identical with local files. Await user test of trophy colours and animated
license card/shadow; remove the gallery args file when returning to menu work.


Hardware confirmation: user reports all prize presentations work, with unfortunate
appearing white. Capture /private/tmp/deko-prizes-hardware-success.log: 55
presentation loads, 3798 presents, zero native draw failures, two-item license
compositions confirmed and no image-pool pressure. menu/unfortunate completed
91 native draws. Existing format documentation identifies unfortunate/prize PRM0
files as untextured white-disc placeholders; the white appearance is consistent
with their content, not evidence of a missing submission. Current src/game
reward flows have no explicit unfortunate reference; consolation uses its own
consolation.prm asset. No claim about every original-game fallback path.

Removed only the args.txt created for the gallery test via FTP; listing confirms
it is absent. Next native app launch returns to normal configured startup.
Multi-item/prize milestone accepted; continue with dynamic resources/shared model
textures and the remaining scene/resource bridges needed by real race startup.


### 2026-11-05 — Native dynamic model and shared-texture adapters

Implemented concrete NativeModel vertex, geometry and texture updates, and derived
models via share_model_textures. A family keeps the original Model/images alive;
derived geometry borrows its immutable GPU images and creates its own descriptor
table, without uploading/copying static texels. Runtime source texture changes
are copied into immutable CPU family revisions. Each model synchronizes changed
revisions into its existing fenced replacement-image slots before drawing. Thus
static GPU originals are shared, but animated replacements currently duplicate
per consumer; a family-wide GPU animation cache is a later optimization.

Batch validation precedes mutation. Texture updates own their input copies and
pad heights below 16 exactly as static uploads do. CPU tests cover atomic failure,
immutable old revisions, logical dimensions/padding and malformed geometry.
Stable layout (counts, indices, LOD, primitive/material state) reuses vertex rings
and updates mesh transforms/bounds. Changed topology builds a replacement Model
with borrowed originals, then commits. Empty geometry remains valid and submits
clear-only work when needed; old geometry never leaks into the new view.

The dedicated upload queue/mutex now lives in a shared FrontendUploads object
created on the owner thread. Derived/root models retain it and the device.
Topology replacement, empty transitions and wrapper destruction retire submitted
reads before sphere-image leases can be released: the family can otherwise keep
an old Model alive beyond its wrapper. This conservative retirement uses queue
idle on those transitions/destruction, not normal stable-layout/texture updates.
Frequently changing particle topology still needs capacity-based optimization.
Legacy race/demo budgets and update paths remain unchanged; dynamic preparation
now honors the active frame-slot count. Foreign-device texture borrowing rejects.

Added developer screen `dynamic`: direct vertex updates on flag 1, stable-layout
geometry updates on flag 2, variable/empty geometry on flag 3, shared animated
32x4 texture padded to height 16, plus an asynchronously loaded Mazda 787B as an
independent-family colour control. A pauses, L toggles colours, R cycles flag 3
4 strips/12 strips/empty, + exits, B returns. Native-only malformed vertex and
texture-batch probes must report rejected=1; GLES skips those probes.

Host tests: 597 passed, 235 skipped, zero failures. Native SDK/UAM and normal GLES
SDK builds pass. Final native build includes explicit shared-resource retirement.
Version 0.6.1-dk-dynamic uploaded to separate Switch app and Desktop. Native-profile
args.txt (previously absent) starts `--screen dynamic`; remove it after hardware
confirmation to restore normal startup. ELF: gt3r-deko-dynamic-debug.elf; prior
verified prize NRO: gt3r-deko-prizes-before-dynamic.nro in build-switch-deko-app.
Hardware validation of the new adapters remains pending.

Final transfer check: NRO and args.txt retrieved from Switch and byte-identical
to local files. Await the dynamic-animation test and then inspect update/rebuild/
texture-sync counters and malformed-input rejection markers.


### Native offscreen textures and environment capture (2026-11-05)

Dynamic resources passed the hardware test (1501 frames, both malformed-input
probes rejected). Developer tools now map physical Switch A to confirm and B to
back without changing game controls.

Added typed render-target snapshots, six-face native environment captures and
DrawItem native image overrides. First capture and resizing initialize all faces;
partial captures publish atomically and keep previous snapshots valid. Target
versions are retained by native consumers as well as SDL readers; model slots
retain sampled images/cubes until their submission fence permits reuse. Five
versions bound memory; long-lived inactive consumers can still exhaust that pool.
Eight external descriptors per model slot cover sphere, six cube faces and image
override. ABI checks now also cover DrawItem. Legacy SDL_GPU APIs stay unsupported
on the native backend; app races remain blocked pending full scene integration.

New `--screen reflections` developer screen combines a real Mazda, reflective
sphere, a secondary camera sampled by an in-world monitor and six SDL face tiles.
L toggles reflections, R freezes the monitor, A pauses, + exits. Native version
0.6.1-dk-reflect; hardware validation pending. 600 host tests pass, 235 skipped;
native SDK/UAM and normal GLES builds pass. Separate experimental app only.


### Native scene lights, fog and mirrored views (2026-11-05)

Hardware confirmed the previous reflection/secondary-camera build. Its log records
1550 main draws, 1364 secondary camera draws and zero target-pool pressure, early
UI batches or failed draws. These are CPU/submission counters, not GPU benchmarks.

Added per-item ItemLights (owned slot upload, validated 0..4 lights), linear/GT3
fog, background/no_fog/helper exclusions, background depth policy, mirrored culling,
opt-in frustum/distance culling and a fragment far-plane limit. RGB5 output and
GT3 compressed-angle/tilted native cubemap lookup are connected. Legacy prototype
uniform defaults preserve its tested fog path. The new scene_state.h helper and
CPU tests cover light replacement/scaling, malformed values, mirrored/background
culling and fog uniform exclusions/scaling. Whole-car far-plane rejection and
full race packet/frame-alpha semantics remain pending.

The `reflections` screen now offers five scenes with left/right: base, animated
red/blue per-car lamps, linear fog, GT3 fog and mirrored view. Its colored backdrop
is deliberately unfogged and the single-sided monitor tests winding. A pauses,
L toggles reflections, R freezes the monitor and + exits. Version 0.6.1-dk-scene
awaits hardware validation. App races remain explicitly blocked.

Final validation: 605 host tests passed, 235 skipped, zero failures. Native SDK/UAM
and GLES builds passed. Switch/Desktop NRO updated; FTP round-trip byte-identical.
Matching ELF: gt3r-deko-scene-debug.elf; fallback: gt3r-deko-reflect-before-scene.nro.


### First native races launched by the app (2026-11-05, hardware pending)

The scene-state viewer passed all modes 0..4 on hardware (1410 main draws, no
failed draws or target-pool pressure). The next milestone uses the real app race
flow rather than another developer screen. RaceScreen now allows the native
backend; physics, AI, HUD, pause/results/audio/save flow are the existing app's.
Native wheel spin blur is disabled for this experimental build.

Environment capture and wet-road scene/blur/composite now use typed native
textures throughout. No SDL_GPU handle casts were added. Native draw scheduling
separates solid/blended phases, respects joined in-order scenery groups, delays
late items and clears depth only at the end of a joined group. Global blending
sorts whole items, not individual meshes. The item cap is 256. Frame-alpha masks,
keep-alpha and destination-alpha additive/lerp blends are connected. Lamp material
emission uses the item's brake factor. Native vertex programs now handle course
environment kinds 2/3 and specular_local road sheen; GS bilinear is available for
wet-road buffers. A separate native fullscreen pass applies the original grade
arithmetic to the completed 3D picture.

Remaining parity work: GT3/VU guard-band subdivision and whole-car far-plane
rejection (GPU clipping currently used), AFAIL RGB_ONLY split passes, replay DOF,
trail and dissolve, and screen glows/vignette inside the grade (currently composed
through SDL after it). This is a first integrated playable build, not final parity
or a measured performance result. The proven standalone race prototype is intact.

610 host tests pass, 235 disc-dependent skips, zero failures. CPU tests cover
joined scenery ordering, depth-reset timing, late draws, transparent-car sorting,
frame-alpha masks/composite states and typed fullscreen grade routing. SDK builds
and Switch transfer must be verified before claiming deployment. Shader inputs
are staged in the container's local temporary filesystem before UAM compilation;
direct mounted reads produced unexpected EOF errors while local copies compiled.

Deployment verified: native SDK/UAM and normal GLES builds passed. Final keep-alpha
blend-cache transition fix compiled successfully. Switch/Desktop NRO updated and
FTP download byte-identical. Removed only the developer args.txt, confirmed absent
in the separate native profile. Matching ELF gt3r-deko-race1-debug.elf; verified
scene fallback gt3r-deko-scene-before-race.nro. Hardware race launch still pending.


### First app race freeze investigation — race2 (2026-11-05)

Race1 hardware test froze without closing. Log deko-race1-failed.log reaches a
completed 32.1 s load, native cube/wet-road buffers and a first 88-item scene
submission, then stops at the next target preparation. No failed-draw or Deko
error marker was recorded. Cumulative allocation totals are not live memory:
initial model upload command buffers have already been released.

New-image creation now explicitly flushes the graphics queue before retiring
completed UI upload resources or allocating a target. A GPU memory unmap can
otherwise occur while prior graphics submissions remain buffered until present.
This is the current synchronization hypothesis, not a hardware-proven root cause.
No per-frame queue-idle was added. Extra target-preparation/upload checkpoints
identify dispatch, retirement, CPU buffer and colour allocation boundaries if the
freeze persists. Native SDK build passes; host/GLES code is unchanged by the fix.
Version 0.6.1-dk-race2 awaits the same actual-race test, no extra viewer tests.


### Initial model upload cost — race3 (2026-11-05, hardware pending)

User reports another freeze but clarifies waiting less than one minute. Race2's
log stops during opponent car uploads, before `Race: loaded` and the first scene;
it does not establish a recurrence of race1's later target-preparation stall.
The next hardware run must allow loading to finish (up to two minutes once).
The original race1 load alone took 32.1 seconds.

Initial command pools are now sized conservatively by image-copy count: 64 KiB
base plus 512 bytes per image, aligned to 4 KiB. Borrowed-texture geometry needs
zero texture-copy slots. Previously every tiny car part allocated/zeroed 4 MiB;
that pool was already released after upload, so this targets load-time allocation
and zero-fill overhead, not persistent memory or measured in-race FPS. Legacy
probe/readback pools retain 4 MiB. Added boundary/alignment/budget CPU tests and
per-upload budget logging. Native SDK build passes; runtime improvement pending.

Final checks: 611 host tests passed, 235 skipped, zero failures. Native SDK build
passed; Switch/Desktop NRO updated and FTP readback byte-identical. Hardware
loading/first-frame result remains pending.


### Native app race reader retention and empty passes — race4 (2026-11-05)

First complete app race passed functionally on race3, but user reports poor
performance. The log records 2795 early UI batches and 2828 reader waits; six cube
views each record hundreds of failed reuse attempts because snapshots remain
retained by native texture consumers. Model sample bindings previously lasted
until the next draw of that model. A car/model no longer drawn could therefore
pin old cubemap faces indefinitely. The five-version pool eventually filled.

FencedReadSlots now owns sampled resources with a copy of each submission fence.
A weak registry in GraphicsState polls completion without blocking and releases
read leases even when the consumer is inactive. Registration happens only during
main-thread drawing, not async model construction. Model destruction still waits
for its own GPU work; no strong registry/owner cycle was introduced. Tests cover
inactive consumers and multiple slots retaining one shared target until all finish.

Empty solid/blended phases for the active LOD are omitted; initial clear and
joined-group depth resets are preserved even without geometry. The native app
now follows the course/camera far plane rather than the legacy Switch 650 m cap.
GLES keeps that cap; reflection capture and rear-view ranges remain intentional.
Car shadows are existing CarVisual geometry routed through the native renderer.

613 host tests passed, 235 skipped. Native and GLES SDK builds passed. Hardware
performance improvement is pending. Do not infer race FPS from whole-app cadence:
the prior trace also includes loading/menu time and a 50.48 s maximum interval.

Race4 deployment: Switch/Desktop updated and FTP readback byte-identical.
Matching ELF race4-debug; verified fallback race3-before-reader-fix.nro.
Await the same race drive to measure the impact; do not claim 60 FPS yet.


### Frame-wide native submission storage — race5 (2026-11-05, hardware pending)

Race4 eliminated all recorded UI/target pressure waits, but user still reports
poor driving/loading/exit performance. Its model summaries aggregate ~47.15 s of
CPU fence waits over 291091 submissions. Two slots per model were repeatedly
reused within one picture across wheels/phases/views. Native trace output also
flushed to SD on every line during model loading/destruction.

The native adapter now uses a shared FrameArena with three fenced frame banks.
Each invocation gets distinct immutable command/uniform/light/fog/descriptor
slices. Dynamic vertex snapshots are versioned within each bank/generation too.
The only routine storage reuse wait is at the frame-bank boundary; a changed
animated texture in the same bank and frame still waits before mutating that
image's staging/pixels. Persistent per-bank texture images remain unchanged.
Frame fences are signalled after all native 3D submissions before UI presentation;
UI rendering does not use this arena. New chunks are reusable 4 MiB allocations,
without redundant zero-fill for ranges fully initialized before use. Native model
loading no longer preallocates separate draw/uniform/light/fog buffers per model.
Command objects are allocated lazily and use shared memory slices. Legacy probe
models without a frame arena retain their existing buffers/submission path.

Per-model retirement now polls/waits its own last submission fences rather than
forcing whole-queue idle repeatedly. Sampled-image lifetimes use a fence list so
several invocations of one model in one bank all retain their resources. The weak
main-thread reader registry still releases inactive consumers safely.

Native trace lines are buffered in RAM (4 MiB bound) and flushed at context/app
shutdown or fatal Deko callback. GT3R_DEKO_SYNC_LOG restores synchronous diagnostic
writes. A CPU crash without orderly shutdown can lose buffered trace details;
the next performance test must quit the app before opening FTP.

617 host tests pass, 235 skipped, zero failures; native and GLES SDK builds pass.
Tests cover frame bank reuse, no per-invocation wait/reset, completed bank reuse,
failed waits preserving old storage, slice alignment/non-overlap/overflow and
multiple unfinished reader leases in the same frame. SDK alignment constants
checked: command memory 4 bytes, uniform buffers 256 bytes (arena slices 256).
Hardware improvement remains unverified; do not infer race FPS from whole-app
cadence which also contains loading and menus.

Final native adjustment: models within one view keep the same render target bound
and avoid redundant per-model fragment barriers. Initial view binding/barrier,
mutated-texture upload barriers and the view completion fence/cache flush remain.
Descriptors use immutable arena slices with nonblocking cache invalidation. Legacy
models without a frame arena retain their original barriers.

Race5 final build passed after barrier adjustments. Switch/Desktop NRO updated,
FTP readback byte-identical. Matching ELF race5-debug; fallback
race4-before-frame-arena.nro. Await hardware validation of load/drive/exit.


### Race5 hardware confirmation — new baseline (2026-11-05)

User confirms a large improvement in driving, loading and leaving the race.
Residual drops occur on the main straight and around opponents. The matching
route5_dry/_787b_91 log records 4736 ms for race loading, zero UI/target pressure,
zero frame-arena reuse waits and 675185 completed reader leases retired. No
failed native draw or Deko error marker was found. Model fence wait averages,
weighted by invocation count, fell from ~0.162 ms/invocation in race4 to
~0.0005 ms/invocation in race5. Sessions differ in duration; do not compare totals.
Arena high-water storage is 243269632 bytes (~232 MiB), reused across frames.

Whole-app cadence still contains menus/loading and is not a race-only FPS metric.
Zero bank waits alone does not identify the remaining bottleneck as CPU or GPU.
Preserve the validated race5 NRO/ELF as fallback. Next profiling should isolate
straight/opponent workload (CPU collection/encoding, primitive count and actual
GPU cost) before choosing another optimization; no graphics reductions applied.


### Live race CPU/GPU profiling — dk-prof (2026-11-06, hardware pending)

Preserves the validated race5 rendering and adds diagnostics to select the next
straight/opponent optimization. Live race markers exclude quick-menu previews,
pauses, demos, replays, finish panels and loading. Cadence is collected only across
consecutive live presents. CPU scope includes animation uploads and separates
simulation, render, collection, environment, draw/glows and acquire/UI/present.
Model summaries count submissions, draws and emitted triangles by pass and role.
CarVisual bodies/wheels/shadows are tagged Car; smoke is Effect; default Scene is
labelled scenery_other. Main-body counts are drawn instances, not unique visible
opponents (split views may duplicate them). Bounded worst-frame records include
position/speed/body count and CPU data.

Sparse GPU timestamp ranges rotate every32live frames after128warmup frames:
environment face, wet scene, wet blur, initial main scenery, remaining cars/effects,
main all, mirror and grade. Only one range is measured in each sampled frame.
The scenery/cars boundary is the first scheduled Car item; the remainder includes
late particles and any other trailing blended items. These are elapsed ranges,
including GPU queue/CPU feeding gaps, not exclusive busy time; stages sampled on
different frames must not be summed as one frame. Deko counter reports flush the
tiled cache, so the diagnostic build has sampling overhead even without CPU waits.

Verified the official SDK contract in devkitPro/deko3d Primer.md and dk_variable.cpp:
ReportCounter writes two64-bit values (counter,timestamp), aligned16bytes. Arena
stores a start/end pair in32bytes and reads only after its normal bank fence.
Sampled banks request cache flush on that fence; no new CPU wait is introduced.
Query command objects are reused, with immutable command slices per marker.
Remaining reports are drained on arena destruction before reporting summaries.
Invalid/unwritten/reversed reports are rejected. Logs remain RAM buffered and
require orderly app exit before FTP retrieval.

Metadata can change offsets/vtables without sizeof changes, so renderer ABI now
also encodes layout revision1 in the feature word's high16bits. A test rejects old
layouts with otherwise identical sizes. CPU tests cover cadence transitions,
warmup/sampling rotation, body/work tagging, bounded diagnostics and invalid GPU
reports.623host tests pass,235skip; native and GLES SDK builds pass. Device query
execution still awaits validation. Matching profiler ELF must accompany the NRO;
keep race5-hardware-validated.nro as the known-good fallback.

Profiler deployment verified: final native/GLES SDK builds passed,623host tests
passed(235skipped), NRO on Switch/Desktop and FTP download byte-identical.
Matching gt3r-deko-prof-debug.elf; race5-hardware-validated.nro preserved.
Next single test:90–120s normal Stage5race with opponents, quit the app, then FTP.


### Clock correction and static packet culling — dk-cache (2026-11-06)

User confirms overclock was active for ~20seconds, then disabled during the same
profiled race. The trace deko-prof-oc-stock.log mixes clock conditions; neither its
cadence mean nor previous unqueried demo/race5 runs establish stock FPS. GPU
reports validated (zero invalid_reports); resource/bank pressure remains zero.
Mixed-run observations guide work only: render CPU16.245ms, simulation2.285ms;
sampled GPU elapsed main18.795ms, initial scenery14.020ms, cars/effects4.808ms.
These ranges are from different frames and include CPU feeding gaps, not busy time.

Added exact placement caching with invalidation before new placement (including
partial failure) and geometry changes. Static scenery and repeat phases avoid
recomputing world*local matrices. Texture-family serial fast path avoids repeated
full revision scans; new geometry resets the consumer serial so animated revisions
reapply. Existing per-bank GPU texture synchronization is preserved. Explicit
cameras bypass placeholder trigonometry/look_at/perspective previously overwritten.

Static Scene-role packets now use their individual assembled primitive spheres
(conservatively enclosed in boxes) against the existing view culler. This rejects
out-of-view primitives even inside a large visible mesh. Only frontend frustum
culling, non-background/non-helper, unscaled modelview and never-updated geometry
qualify. Moving vertices and scaled backdrops retain mesh policy; app car items
have Car role. Original ordering and visible effects/distance remain unchanged.
Counters record packet rejections and placement/texture cache hits.

ClockReader polls read-only CPU/GPU/EMC service rates once per second. Modern
clkrst sessions use the SDK-documented unk=3; older systems use pcv. No clock-setting
API is called. Unknown/denied rates stay unknown. Configured service rates are not
physical clock counter measurements. Frame-tagged changes are logged; delayed GPU
reports resolve the configuration at their original frame. Global summaries still
mix configurations when clocks change and explicitly warn. Keep the next test at
constant stock settings from launch through the race, then quit the app for FTP.

629 host tests pass,235skip; native/GLES SDK builds pass. Tests cover placement
reuse/geometry invalidation/failure, serial atomicity, retrospective clock tagging,
conservative packet bounds, mirrored views, nonuniform scaling and near/far/frustum
crossings. Device speedup and rendering remain pending hardware confirmation.

Cache/cull deployment verified: Switch/Desktop updated, FTP readback byte-identical.
Matching gt3r-deko-cache-debug.elf; prior profiler and hardware-validated race5
NROs preserved. Next run must keep overclock disabled from launch throughout.

### Stock baseline and dedicated-demo comparison (2026-11-06)

The cache/cull race test completed at constant configured service rates: CPU
1020 MHz, GPU 307.2 MHz, EMC 1331.2 MHz (known_mask=7). Its live cadence averaged
26.074 ms (~38.35 FPS); simulation CPU 2.941 ms, render CPU 18.934 ms. Main scenery
recorded ~1189 draws/frame and cars ~509. No native failures, UI pressure waits,
frame-bank waits or invalid GPU reports. This is the first instrumented constant
stock baseline; the preceding mixed-OC run is not a valid performance A/B.

The historical dedicated demo combines scenery and six Mazda instances into one
Model, unlike the app's per-item model submissions. This is a structural difference,
not yet a measured explanation of the FPS gap. The user recalls 60 FPS without OC.

Rebuilt only the separate SDL demo as 0.6.1-dk-compare. It polls the same read-only
clock services, logs clock changes with demo frame numbers, and reports main draw/
triangle counts, preparation/simulation/environment/record/wait CPU and cadence
after 120 warmup frames. It adds no GPU timestamp queries. Existing controls and
rendering are preserved. Demo results are separate from RaceProfile: combined-scene
data must not be mistaken for app per-role counters. This uses the current shared
renderer, not a byte-identical reconstruction of the historical binary.

Demo build/UAM pass; existing missing-field-initializers warning remains. The demo
build now stages shader inputs locally before UAM, as the app build already does.
Old device demo saved at /private/tmp/deko-demo-before-compare.nro. Test the dedicated
GT3 SDL Deko3D Probe for 90–120 seconds at constant stock clocks, default settings,
overlay off, then exit with +; log /switch/gt3r-deko-probe.log. Full app NRO unchanged.

Device comparison result: /private/tmp/deko-demo-compare.log. Demo configured clocks
remain 1020/307.2/1331.2 MHz, known_mask=7, matching the app stock baseline. After
120 warmup frames, 6872 cadence intervals average 16.772 ms (~59.62 FPS), p95 upper
bin 18.25 ms, p99 20.25 ms, maximum 24.520 ms. The user's stock-60 observation is
supported by this measured run. All six physics cars run (five AI opponent summaries).
Main scene averages 1930.90 draws and 50003.67 triangles/frame, record CPU 3.494 ms,
slot wait 2.464 ms; simulation 1.718 ms, environment CPU .123 ms. Preparation 4.562 ms
includes simulation/animation/transforms and must not be added to simulation again.
The app main scene emits fewer draws/triangles (~1698/~43627), yet spends 5.545 ms
recording main commands across ~82 submissions. Scene/frame sequences and effects
are not identical, so this does not prove submission overhead explains the whole
gap. It does rule out treating the demo's 60 FPS as merely an OC observation, and
points to integrated scene preparation/submission/cache churn for further profiling.
Avoid comparing the demo's main-record time against app total-render CPU (~19 ms):
those scopes differ. No performance patch or app deployment in this comparison.

### Immutable arena CPU visibility (0.6.1-dk-cache2, 2026-11-06)

The user notes that demo shadows/other effects differ. Keep those effects in the
app: main geometry counts alone do not establish equal total GPU work.

Inspection of the local SDK source (maxwell/gpu_base.cpp) shows that L2Cache
invalidation emits L2FlushDirty plus L2SysmemInvalidate. The frontend repeated
that global operation, plus texture-data invalidation, at every model's descriptor
binding, despite each invocation using its own immutable arena snapshot.

FrameArena now prepares CPU visibility before the first model command-list consumer
in each generation. Completed bank fences and cache-line-disjoint aligned slices
allow later CPU-written snapshots to use the same preparation: no prior draw can
read or populate those later slices. New chunk mappings conservatively invalidate
the preparation so the next consumer queues another global operation. Commands
have independent immutable arena storage; the reused query DkCmdBuf control list
is consumed immediately by dkQueueSubmitCommands, as for timestamp reports.

Arena descriptor bindings retain descriptor/header/sampler invalidation but omit
the repeated L2 and texture-data invalidation. Fresh arena vertex snapshots use
the same visibility preparation. Mutable animation staging, texture uploads,
view render-to-sample boundaries, reader leases, retirement fences and legacy
non-arena submission barriers retain their synchronization. No effects, shadows,
resolution, draw distance, material ordering or geometry are removed.

Native frame arena summary adds cpu_read_invalidations (all app frames, including
menus, not solely live driving). 632 host tests pass, 235 skipped; tests cover
cross-model/view sharing, bank reuse, failed enqueue/retry and same-frame heap
growth. Native app/SDK/UAM build passes. Hardware speedup and graphics validation
pending. Previous device NRO saved as gt3r-deko-cache-hardware-validated.nro and
/private/tmp/deko-cache-before-cache2.nro; matching old cache-debug ELF retained.
New matching ELF: gt3r-deko-cache2-debug.elf. No commit or push.

Cache2 device log: /private/tmp/deko-cache2-stock.log. Constant service clocks
1020/307.2/1331.2 MHz, known_mask=7. Live cadence 26.874 ms (~37.21 FPS), versus
26.074 ms (~38.35 FPS) in cache1. These are different race sequences: 4+ body
instances account for 3508/4425 intervals (~79%) versus 2806/5676 (~49%); main car
draws increase from ~509 to ~656 and main submissions from ~82 to ~98. Thus no
clear end-to-end speedup or regression established by this run. Heavy-body-bin
cadence improves from 28.299 to 27.230 ms, while 2–3-body worsens from 23.899 to
25.510 ms; bins alone do not control track position or car workload. Scenery CPU
record remains ~3.16 ms. Native arena cpu_read_invalidations=4740 for all app
frames (not just live), bytes=243269632, bank_waits=0; UI pressure waits=0 and
invalid GPU reports=0. No native errors logged. This validates the counter path,
not a claim that invalidations explained the main FPS gap.

### CPU cost breakdown (0.6.1-dk-cpu, 2026-11-06)

The existing total-render scope includes animation uploads and HUD/tail work not
named in the old race summary; its draw scope also includes wet-road preparation.
Main command record time alone is not the full app integration overhead.

Added RAM-only CPU diagnostics, preserving cache2 rendering and effects:
- race_animation_upload_cpu, race_wet_prepare_cpu (nested in draw), and
  race_hud_tail_cpu (HUD/overlays after scene/perf logging).
- Race frontend per-pass validation, target/lease selection, scheduling, and
  dispatch. Phase checks, texture synchronization, model placement/camera and
  model record+wait are nested in dispatch; do not add them a second time.
- Race record stages per pass and main role: setup (including descriptor tables,
  dynamic uploads and fixed state), mesh culling, packet/material command recording,
  and queue submission/fencing. These partition record_cpu and exclude slot wait.

Granular frontend stamps are enabled only in live race profiling. Model stamps
add a fixed few reads per invocation; statistics are aggregated in bounded RAM
and reported on exit, without periodic SD writes. Native GPU sampling remains
unchanged. Added fields in RaceProfileCpu and SubmissionTiming are rebuilt together
with app/engine/native backend. No new performance claim until device log arrives.

632 host tests pass, 235 skipped, native app/UAM build passes. Known aggregate
initializer/link-group warnings remain. Cache2 device NRO preserved as
gt3r-deko-cache2-hardware-validated.nro with matching cache2-debug ELF. New symbols
saved as gt3r-deko-cpu-debug.elf. CPU diagnostic NRO deployed to the experimental
app path and Desktop; normal app startup, unchanged config. Next test: about one
minute of Route 5 driving at stock clocks, normal exit, then ftpd for RAM log flush.

CPU diagnostic result: /private/tmp/deko-cpu-stock.log. Constant stock service
clocks 1020/307.2/1331.2 MHz, no invalid GPU reports, no bank waits. Live cadence
27.598 ms (~36.23 FPS), main submits ~99/frame. Principal newly isolated CPU cost:
animation upload 7.342 ms/frame (p95 8.25 ms), versus HUD/tail .783 ms and wet-road
preparation .437 ms. Main model recording 5.763 ms partitions into setup 2.750,
mesh culling .138, packets 2.762, queue submit/fences .114 ms. Car-role setup 2.022
ms dominates its packet recording .679 ms. Main frontend validation .084, target
.154, scheduling .137, dispatch 6.654 ms (nested model 5.830, phases .220, textures
.082, placement .240). Hence raw queue submission calls are not the primary CPU
cost; neither are phase scans or item transforms. Animation uploads are the next
investigation target: CourseScene::upload_animations (CLUT refresh + moving nodes),
wet/billboard CLUT uploads, TextureUpdates::apply and stable-topology updates. The
7.342 ms includes these activities; do not attribute it solely to palette decoding
without finer attribution. Preserve animated effects. No app change/deployment in
this log-analysis turn.

### Animation CPU storage reuse (0.6.1-dk-anim, 2026-11-06)

TextureUpdates now recycles exclusively owned CPU pixel vectors instead of
allocating/value-initializing a full image for every palette revision. Retained
shared snapshots force a new allocation. All batch allocations are planned before
mutating any revision; validation, serial exhaustion and failed allocations leave
published state untouched. Raw borrowed input pointers are excluded from reuse,
including cross-texture swaps, and duplicate indices retain last-update-wins
semantics. Full-height images use one contiguous copy; tiny-image vertical
expansion retains the exact prior mapping. GPU images/staging/leases are unchanged.
Family destruction reports revision, allocation and reuse totals.

Native GpuModel::update_geometry omits per-primitive sphere reconstruction only
in the generic CPU metadata copy, which native views bypass. Public mesh bounds,
indices, materials, vertex counts and optional retained CPU geometry still update.
Actual native-model upload retains its original bounds; other rendering backends
retain their guard-band packet spheres by default. This removes repeated work
without changing native draw ordering, culling, animation rate or visible effects.

For attribution, live animation uploads temporarily enable the existing RAM-only
render_profile scopes, restoring the previous setting afterwards. New native race
summaries distinguish palette refresh, moving-node mesh emission, texture revision
preparation, native geometry updates and generic geometry metadata. These are
nested components of animation_upload_cpu, not extra times to add to it.

Host tests cover exclusive reuse versus retained snapshots, bad-batch atomicity,
duplicate updates, raw-input swaps, full-height copying, tiny-image expansion, and
metadata bounds omission with identical geometry/material packets. Device speedup
is not yet measured. Previous CPU diagnostic NRO preserved as
gt3r-deko-cpu-hardware-validated.nro with matching cpu-debug ELF.

636 host tests pass, 235 skipped; native app/UAM build passes. Version
0.6.1-dk-anim deployed to the experimental app path and Desktop; matching symbols
saved as gt3r-deko-anim-debug.elf. Next run: Route 5, constant stock clocks,
animations/effects enabled as before, normal exit before retrieving the RAM log.
No config changes, commit or push.

### Car-shadow isolation A/B (0.6.1-dk-shadow, 2026-11-06)

The user prioritizes the unexplained demo/full-race gap over metadata savings.
Existing video.car_shadows=off also omits the headlamp pool, so it cannot isolate
only shadows. Added a native-app-only diagnostic video.deko_test_no_car_shadows
(default off), read at race setup. CarLook.draw_shadows (default true) gates only
node1/lamp/sun and node6 underbody shadow items. Node5/headlamp pool, its geometry,
its ground placement/depth bias and all remaining effects stay on their normal
path. Shadow resources are still prepared, preserving pool setup and avoiding an
unrelated loading change. Generic renderer/default behavior remains unchanged.

RaceProfileInfo records shadows_disabled. Exit summary logs live frames with and
without the diagnostic; setup logs the selected state. No mixed-mode FPS claim.
First device config prepared by adding only deko_test_no_car_shadows=on under
[video]; original backed up at /private/tmp/deko-shadow-config-before.ini. Restore
off for the second run using the identical NRO, leaving all other settings alone.

Repeatable initial test protocol: same Arcade event, Route5, Mazda, starting grid,
default external camera, stock clocks; remain stationary without changing camera
for 60 seconds after the start lights, quit normally, open ftpd. Repeat identically
with diagnostic off. AI still move; this controls player position/camera and event
start, not every GPU frame. Inspect workload/time bins before attributing differences.
This stationary test does not establish worst-case shadow cost while following cars.
Prior anim NRO preserved as gt3r-deko-anim-hardware-validated.nro; matching anim ELF.

636 host tests pass, 235 skipped; native SDK/UAM/app build passes. Shadow diagnostic
NRO deployed to experimental Switch app and Desktop; FTP readback matches. Config
for first no-shadow run uploaded and verified, original also saved as
build-switch-deko-app/shadow-config-before.ini. Matching shadow-debug ELF saved.
Next user completion should retrieve the log before changing the diagnostic back
to off and requesting the identical stationary 60-second run with shadows present.

Shadow A/B logs preserved as /private/tmp/deko-shadow-off-stock.log and
/private/tmp/deko-shadow-on-stock.log. Both: stock service clocks
1020/307.2/1331.2 MHz, all retained GPU slow samples at (-147.3,3.8), speed0,
correct all-off/all-on diagnostic frame counters and zero invalid GPU reports.
Live cadence off24.722 ms (~40.45 FPS), on26.367 ms (~37.93 FPS).
Sampled scenery elapsed stays16.833/16.812 ms; cars/effects3.147/4.375 ms;
main-all19.884/21.882 ms. These are non-exclusive elapsed ranges, not GPU busy.
CPU render18.081/19.401 ms; main command recording4.332/4.552 ms. Animation
uploads differ5.956/6.762 ms despite unchanged animation settings. Run duration
also differs (~72.5 vs90.7 seconds of cadence intervals), and AI still move, so
1.645 ms overall cadence delta is an observed difference, not an exact isolated
shadow cost. Off still averages24.7 ms rather than16.7 ms: shadows contribute
but do not explain the demo/full-race gap alone. Original shadows now restored
via deko_test_no_car_shadows=off. No new deployment/config change in analysis.

### Resolution sensitivity diagnostic (pending device run)

Continue isolating the ~16.8 ms sampled scenery range before attributing it to
exclusive GPU work. Current config backed up as /private/tmp/deko-fill-config-before.ini.
Uploaded and readback-verified a single-setting change: video.render_scale1.0 to
0.5. Shadow test remains off (actual shadows ON), all other settings unchanged.
Existing native View3D sizes main targets by this scale; expect640x360 instead of
1280x720, unchanged display/UI size, geometry and animations. Fixed-size offscreen
passes need not scale. Same NRO; no source/build/commit/push changes for this test.

Repeat stationary Route5 at the same start/camera for60 seconds, stock clocks.
Compare target dimensions, emitted geometry and sampled ranges with shadow-on
baseline; resolution sensitivity is evidence about pixel-dependent work, not a
precise CPU/GPU busy split. Restore render_scale1.0 after capturing the result;
this is a diagnostic, not an accepted quality reduction.

Result /private/tmp/deko-fill-half-stock.log confirms640x360 main target, shadows
ON, constant stock clocks, retained GPU samples all(-147.3,3.8), speed0, invalid0.
Compared to720p stationary shadow-on baseline: main_scenery sampled elapsed
16.812->5.255 ms, main_all21.882->7.477 ms, cars/effects4.375->2.240 ms. These are
elapsed ranges with diagnostic overhead and potential CPU feeding gaps, but the
large resolution sensitivity is strong evidence of expensive pixel-dependent
work. Cadence26.367->23.353 ms (~37.93->42.82 FPS) improves much less; render CPU
19.401->17.473 ms, animation6.762->5.716 ms. Simultaneous CPU work remains a
limiter; a GPU-only fix is not sufficient evidence of60 FPS. Run duration and AI
work differ (~121 versus91 seconds; main draws1512 versus1570), so avoid claiming
precise isolated speedups. Fixed reflection/blur samples stay effectively equal.
Restored render_scale1.0 in current device config, FTP readback verified. No source
or NRO change. Next investigate main-scene pixel work (shader/material paths,
overdraw, texture access and output settings) versus the dedicated demo while
retaining the independently identified animation/metadata CPU target.

### Native metadata-only geometry refresh (0.6.1-dk-meta)

GpuModel::update_geometry previously packed full CPU vertex/index arrays again
after successful native backend updates, even without keep_geometry. Introduced
describe_model_geometry: computes unmerged public mesh/material/offset/bounds
metadata directly, counts only valid complete triangles, and reuses metadata and
primitive-vector capacity. No packed vertex/index copies or temporary per-packet
index vectors are needed. Optional retained CPU geometry keeps the existing
assembly path. Native uploader geometry/culling/packet order and GLES unchanged.

638 host tests pass,235skip; native app/UAM build passes. Equivalence tests cover
malformed triangles, texture clamping, empty/shrinking models and repeated vertex
changes; native wrapper integration checks counts/bounds/transforms, keep_geometry
and failed backend updates. Prior shadow NRO saved as
gt3r-deko-shadow-hardware-validated.nro; new matching meta-debug ELF saved.
Device speedup pending; existing metadata CPU counter measures the changed scope.
Use stationary Route5 60-second test at restored720p with shadows/effects on.
This addresses measured CPU duplication, not a claim that the pixel-dependent
GPU cost or the whole demo gap has been solved. No config change, commit or push.

Meta device result (/private/tmp/deko-meta-stock.log): cadence26.487 ms (~37.75
FPS), metadata2.427 ms, animations6.660 ms, render19.342 ms. Main draws1570.22
and triangles37817.79 closely match stationary shadow-on baseline. Stock clocks,
shadows ON, no invalid GPU reports/bank waits. No speedup demonstrated.

Follow-up source audit found CourseScene::set_moving unconditionally calls
keep_geometry(), so the real animated scenery bypassed the new metadata-only
path. Previous validation covered both wrapper modes but missed this caller.
Corrected in0.6.1-dk-meta2: only non-native views request retained CPU moving
geometry. The sole production geometry() reader is the generic View3D CPU clipper;
native View3D returns through NativeView before that path and uses GPU clipping.
Other renderer retention and explicit keep_geometry contracts remain unchanged.
No shader, effects, scale or config change. Native app/UAM rebuild passes; meta
NRO archived as gt3r-deko-meta-before-course-fix.nro. Next hardware log must confirm
the metadata counter actually drops before claiming a speedup.

### Arcade allocation failure and inherited policies (0.6.1-dk-resources)

User reported slow Arcade car loads, a crash after a licence/Arcade sequence and
stepped licence reflections. Performance experiments paused for stability.
Preserved /private/tmp/deko-meta2-crash.log and latest Atmosphere report
/private/tmp/deko-meta2-atmosphere.log (01793981299_01004ab00a260000.log).
Log ends with Deko result1 (Fail), dkMemBlockCreate initialization failure during
Trial Mountain/Corvette race loading, not an OpenGL call. Matching meta2 symbols
resolve main-thread fatal callback -> _Exit -> libnx shutdown; crashed secondary
thread is loudness measurement doing file I/O during shutdown. Do not label the
primary logged failure an audio bug or proven system-RAM exhaustion. SDK source
returns Fail for NV mapping/creation failure, separately from OutOfMemory.

SDK nv.o reveals weak __nx_nv_transfermem_size=8 MiB. Native app now overrides
to32 MiB for NV driver bookkeeping; strong symbol and actual0x02000000 ELF data
verified. Production/GLES app retains its original SDK policy. This is a targeted
capacity mitigation, not proof of the exact exhausted driver allocation.

Reduce mapping pressure directly: native per-model vertex/index/descriptor storage
is packed into one aligned allocation (previously three). Image storage remains
separate. Frame descriptor templates are cached CPU vectors, no longer three
GPU-mapped blocks per model; each submitted table is still an immutable fenced
arena snapshot. Dynamic image staging/upload barriers and resource leases remain.
Tests cover slice alignment, overflow, disjoint contents; native SDK build validates
all address/descriptor changes, device rendering/lifetime validation pending.

Audit confirms native GpuContext adoption bypasses GLES initialization and its
freeze/PBO/UBO flags; native texture updates return before GLES freeze logic.
However native reflection capture was64x64, one face per frame, inherited as a
reduced policy. Now native deko_reflection_faces defaults6 (all faces every frame),
optional1 is explicit native policy. Per-race face cursor replaces a static global;
GLES cadence stays unchanged. Capture resolution remains64 per face. All-face
updates add work: do not compare FPS against earlier one-face baseline as a pure
optimization. Direct legacy glFinish diagnostic is now guarded away from native.
Generic options such as full_car_detail remain shared; current config explicitly
sets it off and was not silently rewritten. Valid CPU culling/glow visibility
optimizations and pending native feature exclusions remain; do not claim every
historical Switch policy has been removed.

640 host tests pass,235skip; native app/UAM build passes. New matching resources
debug ELF preserved, previous meta2 NRO archived. Experimental NRO copied to
Switch/Desktop; config unchanged (720p, shadows on, no reflection override ->6).
Next test is original reproduction: licence, scroll Arcade car previews, start
the race that crashed, check reflections/geometry and normal exit/log retrieval.
No commit or push. Crash resolution must be confirmed on hardware.

Resource-build device result: /private/tmp/deko-resources-trialmt.log. Trial
Mountain AEST0001/Corvette race loads in8181 ms and runs without the previous
allocation error; menu previews~153–260 ms. This log contains the Arcade race,
not the preceding licence sequence of the original report, so do not claim every
original transition has been reproduced. Pool32 MiB, reflections6faces/frame,
stock1020/307.2/1331.2MHz confirmed. Live cadence19.730 ms (~50.68 FPS), render
CPU10.060, simulation2.293, animation2.423, metadata.147 ms. Different course
from earlier metadata benchmark: no direct percentage speedup claim. Main sampled
elapsed13.834 ms and post-grade3.066 ms (non-exclusive ranges on different frames,
do not add as an exact frame budget). No invalid GPU reports or bank waits.

### Precomputed native colour-grade contributions (0.6.1-dk-grade)

The original post shader evaluates the grade curves, saturation coefficients and
byte truncations per pixel. New helper precomputes three256-entry RGB contribution
rows in a256x16RGBA8 texture (16 physical rows avoid the uploader's tiny-height
expansion; only rows0..2 are used). Contributions are nonnegative; individually
clamping each to255 preserves final min(sum,255), even at high saturation.
Shader rounds the source to bytes as before, fetches three unfiltered texels,
adds/saturates and preserves alpha. Effects, grade parameters and resolution stay.

Each FrontendView caches its LUT by all six grade parameters, rebuilds only on
changes and binds via the post pass's otherwise-unused sphere slot. The normal
sampled-resource fence lease retains old LUT images if a preset changes while
prior draws remain in flight. Invalid/nonfinite settings are rejected before
upload. No permanent global cache or unbounded texture history is added.

643 host tests pass,235skip, native app/UAM build passes. New tests compare lookup
results against independent existing post_grade_pixel over channel ramps, mixed
colours, inverted/equal levels, saturation0/>1/negative and curve changes, plus
invalid settings. Hardware colour/speed validation remains pending; floating-point
GPU/CPU rounding must still be checked visually. Prior resources NRO archived as
gt3r-deko-resources-trialmt-validated.nro, matching resources ELF retained; new
matching symbols gt3r-deko-grade-debug.elf. Test same Trial Mountain race at stock,
check colours/brightness and exit normally for log comparison. No config edits,
commit or push.

Grade device result (/private/tmp/deko-grade-trialmt.log): same Trial Mountain
AEST0001/Corvette, stock1020/307.2/1331.2MHz throughout. Live cadence18.397 ms
(~54.36FPS), versus19.730 ms (~50.68FPS). Sampled post-grade elapsed3.066->1.846
ms (~40% lower), main-all13.834->13.663 ms, UI CPU4.756->3.499 ms (includes waits,
not pure UI arithmetic). Main work~1275draws/35651triangles versus1262/35227;
race sequences/durations differ, so not every overall-FPS change is a controlled
grade-only delta. LUT rebuilt twice, not perframe. No invalid GPU reports or bank
waits. User reported test completed, without explicit visual-quality feedback.
Hardware colour fidelity remains subject to observation; do not assert verified
pixel-identical GPU output from CPU tests alone. Frame average still above16.67
ms, p95 cadence23.25 ms, main GPU sampled max17.7 ms: stable60 not achieved.
Further target is main scene pixel/overdraw work and car-heavy peaks; no additional
source/config/deployment change in this log-analysis turn.

### Race body back-face material policy (0.6.1-dk-body)

Audit found native rasterization still explicitly excluded car meshes from back-
face culling as a bring-up policy, except sphere-map items. Generic View3D uses
Back by default and respects kDoubleSided. Native race body items already carry
car_surface, distinguishing them from wheels/shadows/headlamp pools/morph helpers.

New native_packet_cull helper includes car_surface in the existing material
policy. Preserves double-sided flags, explicit overrides, single-sided forcing,
mirrored front/back inversion, background exclusion and legacy probe behavior.
Wheels, projected shadows, lamps and untagged helper meshes retain prior policy.
No draw reordering, material/alpha/depth change, resolution or effect removal.
Counters log backface-enabled vs two-sided body packets at model destruction;
these are submitted policies, NOT measured GPU rejection counts.

Independent offline check of existing private Mazda packets: route5-061.gtdk base
body has3250 normal-aligned triangles,15 opposed,2 degenerate/zero; brake variant
3254/15/2. This supports the expected winding, not proof for every vehicle or
camera. No indices/normals are rewritten. Hardware body/glass validation required.

645 host tests pass,235skip; native app/UAM build passes. Tests cover body material
flags, mirrors, explicit overrides/backgrounds and unchanged wheel/shadow/helper/
legacy policy. Grade baseline NRO saved as gt3r-deko-grade-trialmt-baseline.nro;
matching grade ELF retained. New matching symbols gt3r-deko-body-debug.elf.
Experimental NRO deployed to Switch/Desktop. Next: same Trial Mountain race at
stock for~90seconds, opponents ahead if possible; check body/glass from different
views for missing/reversed faces. Performance gain remains unmeasured. No config
change, commit or push.

Body-culling device log: /private/tmp/deko-body-trialmt.log. Constant stock clocks,
same Trial Mountain event/Corvette, no invalid GPU reports. Body counters confirm
2472229 backface-enabled submitted packets,0 two-sided packets for the bodies
drawn in this run (not actual GPU-rejected triangle counts). Live cadence18.787
ms (~53.23FPS), versus grade baseline18.397 (~54.36FPS). Work differs substantially:
main car draws522.36/frame versus339.03 (+54%), car triangles13021.83 versus
8849.91 (+47%); 4+ body intervals2591/5879 (~44%) versus1064/5562 (~19%).
Thus no isolated speedup/regression can be concluded from the overall average.
4+ body cadence19.510 versus20.592 ms is encouraging but not position-controlled;
do not quote it as a measured culling gain. Sampled scenery10.111 versus10.026 ms,
cars/effects4.033 versus3.202, main-all14.261 versus13.663; grade1.822 versus1.846.
User reported test completed, without explicit visual-quality feedback. No new
deployment/config change in this log-analysis turn; scenery remains the principal
average sampled range. Preserve the grade baseline for controlled comparison.

### GLES separation audit and ordinary scenery shader (0.6.1-dk-common)

User asks whether every OpenGL trace is gone. Answer: no. The shared engine and
experimental target still link EGL/GLESv2/glapi/drm_nouveau; legacy renderer code
remains. Normal native startup creates only Deko and adopts NativeRenderer instead
of GLES initialization. Native draw/update/destruction returns before GL branches;
direct race glFinish is guarded away. This is not a compile-time GLES-free binary,
nor evidence that GL runs a second scene. Shared settings (e.g. explicit config
full_car_detail=off) and some shared Switch policies remain. Do not claim that
removing linked libraries alone removes measured GPU pixel work.

Scenery specialization: compile model.frag twice. Common variant inserts
GT3R_COMMON_FRAGMENT after #version and excludes only the unreachable special-mode
branches. Exact source tail, alpha/far tests, GS sampling, fog, RGB5 and arithmetic
remain shared. CPU selects it only for Main pass Scene-role items with final shader
mode exactly0 or1; all cars, effects, menus/Other, environment, wet, mirror and post
passes retain the full shader. Reflection/sphere/debug modes2..8 always use full.
Fragment stage is switched only when selected program changes; vertex stage stays.
Counters log common/general submitted packets and program switches at destruction.

SDK DKSH metadata: full fragment code15616 bytes/21GPR, common3072 bytes/20GPR,
both0 scratch. This proves smaller code, not a measured GPU speedup or occupancy
improvement. App/prototype shader builders both stage/compile the added asset;
dedicated legacy demo leaves the optional common shader pointer null. All native
model replacements receive the same shader pointers. Code pool128KiB has headroom.

646 host tests pass,235skip; native app/UAM build passes. New selection test covers
all special modes, no shader available, legacy/capture/post paths and every
non-Main pass plus car/effect roles. No scene ordering, geometry, effects, resolution
or config changes. Prior body NRO saved as gt3r-deko-body-trialmt-baseline.nro,
matching body ELF retained; new common-debug ELF saved. NRO deployed to experimental
Switch/Desktop. Next same Trial Mountain~90seconds stock; check textures, fog,
brightness and log shader coverage/switch counts and sampled main-scene elapsed.
Performance and graphics validation pending hardware. No commit/push.

### Per-frame ordinary resource retirement (0.6.1-dk-retire)

Latest Trial Mountain CPU profile attributes1.898 ms/frame to environment-view
target preparation across six views, versus.711 ms dispatch. This target scope
includes global reader/image sweeps, version selection and other setup: not an
exclusive GC timer. Source repeats both global sweeps before every native view.

FrameResourceSweep schedules the ordinary pre-view sweep once per actual present
epoch. All six cube faces/main/post can share it; early UI flushes do not advance
the epoch. Pool-pressure paths force a fresh sweep after submitting pending UI,
and existing UI-reader/write-fence waits remain. Upload, descriptor-exhaustion,
UI-recording and UI-drain collection calls stay independent and unchanged; do not
claim every collection subsystem runs only once. Image uploadSubmitted is cleared
after initial upload completion/staging cleanup, avoiding repeated polling of an
already-completed immutable upload. Target writes retain separate fences/leases.

Counters report scheduled_sweeps/skipped_views/pressure_sweeps. 648 host tests
pass,235skip; native app/UAM build passes. Tests combine real retained-resource
weak references with eight same-frame views, unfinished fences, GPU progress and
forced same-frame pressure collection, next-frame cleanup and callback retry.
No effects, shadow policy, six-face reflection cadence, resolution or shaders
change. Prior common NRO saved as gt3r-deko-common-trialmt-baseline.nro; matching
common ELF retained. New symbols gt3r-deko-retire-debug.elf; experimental NRO
deployed to Switch/Desktop. Device gains and pool behavior pending validation.

User asks whether gains apply to Special Stage Route5. Metadata/resource/common
Main-scene shader paths are shared; grade LUT benefits only tracks with grade on.
Night adds wet-road/lighting/glows and now has six-face reflections unlike older
Route5 baselines, so no transferable FPS claim or direct old/new percentage.
Next run Route5~90seconds at stock, same usual car/camera, include traffic, normal
exit and ftpd. This is a current night baseline and a targeted resource-path check,
not a controlled per-patch speedup comparison to the old one-face build.
No config changes, commit or push.

Common-shader device result: /private/tmp/deko-common-trialmt.log. Same Trial
Mountain event/Corvette, constant stock1020/307.2/1331.2MHz, invalid GPU reports0,
bank waits0. Live cadence17.851 ms (~56.02FPS), versus body18.787 (~53.23FPS).
Sampled scenery10.111->8.142 ms, main-all14.261->12.158, cars/effects4.033->4.002,
post1.822->1.852 ms. Main triangles40536 versus40188, car triangles14417 versus
13022; moving traffic/camera/race duration still prevent a strictly isolated A/B.
All traffic bins improve in average (0/1 bodies17.171->16.845,2/3 bodies19.284->
17.558,4+bodies19.510->18.634 ms), not matched positions. CPU render10.829->11.254
ms, main record2.864->3.226; UI CPU including waits3.064->1.564 ms. Main car draw
count increases522->563. Counters show5641028 common packets,5362045 general,
56439 switches over all model lifetimes: specialization used extensively without
per-packet program churn. No fresh source/config/NRO change in this analysis turn.
Average remains above16.67ms, p95 cadence24ms; do not claim stable60. User reported
completion, no explicit graphics feedback. Next investigate remaining ordinary
fragment arithmetic and main CPU cost while preserving this tested baseline.

Animation optimization device result: /private/tmp/deko-anim-stock.log. Clocks
constant at 1020/307.2/1331.2 MHz. Live cadence 25.682 ms (~38.94 FPS), render CPU
18.438 ms versus prior diagnostic 20.251 ms; animation upload 6.331 ms versus
7.342 ms (~1.01 ms /14% lower). Race scenes differ (main submissions ~93 versus
99 and draws ~1788 versus1923), so not all cadence/render change is attributable
to the patch. Pixel storage records 49 allocations and140883 reuses across three
families. No bank waits or invalid GPU reports.

New attribution: palette refresh .320 ms, texture revision .091 ms, moving-node
emission 2.693 ms, backend geometry updates .737 ms and generic CPU metadata
reassembly 2.422 ms. Thus most remaining animation CPU work is moving geometry
emission plus redundant generic metadata rebuilding, not palette recolouring.
Next target is the native GpuModel::update_geometry metadata assembly path, which
still packs/sanitizes index/vertex lists despite native backend owning its own
geometry. Preserve public metadata/counts and optional retained CPU geometry;
do not simply drop meshes_ or skip updates. No new deployment in this analysis.

### Retirement Route5 result and reflection projection (0.6.1-dk-envmath)

Retirement device log: /private/tmp/deko-retire-route5.log. Arcade
G_80sports_n_0002, route5_dry, Mazda787B; clocks constant1020/307.2/1331.2MHz,
six64x64 reflection faces per frame. Live cadence22.436ms (~44.57FPS), p95
upper28.75ms, p99upper33ms. CPU render14.311ms, simulation2.575ms, animation
upload3.664ms (moving emission2.256ms, native geometry.733ms, metadata.222ms),
glows2.325ms. Sampled main-all elapsed18.778ms, scenery13.241ms, cars/effects
5.447ms. GPU elapsed includes feeding gaps and ranges from different frames must
not be added. Main workload~1786draws/45573triangles per frame. No post-grade
samples on this night preset, so the grade LUT cannot explain a direct night gain.

Resource sweep counts:5955scheduled,47720skipped views,0pressure sweeps. This
confirms coalescing is active without observed pool pressure. Environment target
preparation.424ms across six views, but no matched Route5 pre-retirement run:
do not attribute an isolated FPS delta to the sweep change or compare it directly
with older one-face reflection night runs. Arena327155712bytes,bankwaits0,
GPUinvalid0. Initial live stalls remain (max282.93ms); stable60 not achieved.

The GT3 car environment projection previously computed acos, sin, asin, atan,
sin/cos per fragment. On the regular path, half/double-angle identities replace
these with square roots and xy normalization, preserving the projection, tilt,
cube sampling, strength and reflection update rate. Tiny xy retains the original
trigonometric path to preserve pole/seam behavior without amplifying coordinates.
No effect removal, resolution, shadow, reflection-face or config change.

CPU counterpart tests compare against an independent double-precision original
trigonometric reference across65536 random normalized and unnormalized vectors,
plus pole/seam/z-clamp cases. Direction and unit-length error <2e-5 in tests;
this is not proof of pixel-identical shader output on hardware. 650 host tests
pass,235skip; native app/UAM build passes. General shader15872bytes/22GPR/0scratch
(previous15616/21/0, because the rare fallback remains); common shader stays
3072bytes/20GPR/0scratch. No speedup is claimed before device measurement.

Retire Route5 NRO archived as gt3r-deko-retire-route5-baseline.nro, matching retire
ELF retained; new symbols gt3r-deko-envmath-debug.elf. Next same Route5/Mazda race
~90seconds at stock, traffic visible, check body colours/reflections for seams or
shimmer, exit normally and open ftpd. Experimental NRO uploaded to Switch,
FTP readback byte-identical, Desktop copy updated. No commit/push.

### Environment-math result and pre-raster distance clipping (0.6.1-dk-clip)

Device log /private/tmp/deko-envmath-route5.log: same Route5/Mazda event, stock
1020/307.2/1331.2MHz, six reflection faces, shadows on. Live cadence22.614ms
(~44.22FPS), versus22.436ms/~44.57FPS. Main draws1875 versus1786, triangles47445
versus45573, car draws649 versus608;4+ body frames~81.5% versus~64.1%. Sampled
cars/effects5.804 versus5.447ms, scenery12.879 versus13.241, main-all18.468 versus
18.778. These are elapsed samples including feeding gaps, not pure GPU busy time.
No isolated speedup/regression from reflection math can be established with these
different workloads.4+ body cadence22.937 versus23.299ms is suggestive, not a
position-controlled comparison. Render CPU14.738ms, simulation2.583ms, moving
emission2.483ms, glows2.427ms. GPUinvalid0, bankwaits0, pressure sweeps0. User
reported completion, no explicit visual-quality feedback. Preserve envmath NRO
as gt3r-deko-envmath-route5-baseline.nro and its matching envmath-debug ELF.

Source audit: far_clip was applied by model.frag discard, after reaching fragment
processing. Move the same positive half-space (far+dot(viewZ,worldPosition)) to
model.vert gl_ClipDistance[0], before any early returns; remove the fragment far
discard. Deko initializes ClipDistanceEnable=0xFF (SDK gpu_3d_base.cpp); UAM
compiles the clip-distance export. Existing scene_fog supplies exactly the same
item-adjusted distance and zero for backgrounds/helpers; post also forces zero.
Zero/negative limits export1, so these passes remain unclipped by this plane.
Capture uses its prior zero limit. Keep projection/depth, alpha tests, fog,
materials, car reflections, shadows, resolution and the configured far distance.
This does not implement the pending GS guard-band subdivision behavior.

ViewCuller also receives this optional seventh plane from the same view and
packed distance. Fully distant mesh/primitive bounds can now be rejected before
submission; bounds crossing the plane remain. Background culler omits it. Other
callers retain the original six-plane behavior through the zero default.

652 host tests pass,235skip; native app/UAM build passes. Added tests cover fully
distant/crossing/exactly-on-plane bounds, disabled/negative limits, translated/
rotated/mirrored views and transformed/nonuniformly scaled geometry; half-space
signs match the prior pixel depth predicate over a point grid. A fixed3e-5m test
tolerance initially failed at600m: standalone diagnostic found exactly one ULP
(6.1035e-5m) reassociation error and zero sign differences. Test now uses a
scale-aware two-epsilon rounding bound, not a changed production plane.
Existing scene_fog tests cover scale-adjusted distance and helper/background
exclusions. Common fragment2816bytes/20GPR, general15616/22, vertex6144/35;
all0scratch. No hardware speed/visibility claim yet. Matching new symbols saved
as gt3r-deko-clip-debug.elf. Next same Route5/Mazda~90seconds stock, check track
and buildings for new popping/holes, normal exit and ftpd. Experimental NRO
deployed to Switch with byte-identical FTP readback; Desktop copy updated.
No config change, commit or push.

### Rejected clip-distance experiment: hardware regression and full rollback

0.6.1-dk-clip is REJECTED. User reports severe visual glitches; downloaded latest
Switch screenshot2026110618495600-1E1800B8D04F999C436DDFE2B8CD0B81.jpg to
/private/tmp/deko-clip-glitch.jpg. Image shows large black rectangular missing
regions on nearby road and clipped/missing car surfaces, with HUD/sky still
present. This is not acceptable far-distance popping. Log saved separately as
/private/tmp/deko-clip-glitch.log; GPUinvalid0 does NOT imply visual correctness.
Do not interpret its shorter timings as an optimization gain with missing pixels.

Immediately restored archived0.6.1-dk-envmath NRO to experimental Switch and
Desktop. FTP readback /private/tmp/deko-envmath-restored-readback.nro matches the
archived baseline byte-for-byte. Removed the full experiment from source: vertex
clip export, seventh CPU plane/helper and its isolated tests; restored original
fragment distance discard and six-plane culler, version0.6.1-dk-envmath. Earlier
native improvements remain. Native rebuild/UAM pass;650 host tests pass,235skip.
Mathematical CPU equivalence and UAM compile success did not validate on-device
clipping/interpolation. Exact GPU cause remains unisolated; do not assert a UAM
compiler bug or particular Deko state fault without a minimal reproduction.

Rejected NRO retained as gt3r-deko-clip-rejected.nro, matching clip-debug ELF kept
for isolated diagnosis. Existing envmath baseline/symbols remain. No new
optimization mixed into the rollback, no config change, commit or push.
Local default NRO/ELF outputs also restored from the archived matching envmath
pair after checking the rebuild; Switch/Desktop use the archived baseline, not
an untested replacement. Rebuilt vertex/full fragment/capture/UI shaders match
baseline bytes; common fragment has two instruction-byte differences with equal
size/GPR count (cause not investigated). No claim of a bit-identical rebuild.
