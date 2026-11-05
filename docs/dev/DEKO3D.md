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
