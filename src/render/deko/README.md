# Native Switch renderer integration

The native view and independent startup harness are hardware-confirmed. Real
application menus also passed their hardware test (3804 frames). A separate app
now has hardware-confirmed showroom cars and background loading. Multi-item
composition and animated prize presentations are implemented, awaiting hardware tests.
The production executable still uses GLES.

## Entry point and current capabilities

`backend.h` exposes `render::deko::create_renderer(window)`. Initialize SDL video
and ROMFS first; the window must outlive the backend and its models/views.
Adopt the result through `GpuContext::adopt_native`. The implementation still
lives in the probe graphics module during migration, but this factory initializes
only device, swapchain, shaders, default texture, descriptors and SDL services.
It does not load a race, physics bundle or model packet.

The concrete NativeRenderer uploads an ordinary static scene::Model and draws
up to 64 ordered static model items per view via GpuModel/View3D, with showroom directional lights,
material lighting, a typed native sphere map and floor-mirror composition.
Animated prize transforms, primitive sphere maps and MultiplyDest are supported.
Dynamic vertex/texture updates and shared derived geometry are now implemented
(awaiting hardware confirmation). Full DrawOptions and SDL_GPU offscreen texture
interoperability remain unsupported.
The separate application adopts the native context for menu cars and prizes;
race startup remains gated. Production and native upload share CPU model
preparation in render/model_geometry.*; native preparation preserves packet order.
Model texture samplers share a Deko3D descriptor set with runtime-selectable
1x, 2x, 4x, 8x and 16x anisotropy. Changing the level waits for queued GPU work
before updating the shared descriptors, so already loaded models take effect.

## Shared resources and SDL contract

- `SDL_render_deko.c` builds into the isolated patched SDL archive using matching
  private headers. The preparation script verifies source/archive ABI, retains
  the AUDOUT fix and records source/contract hashes.
- `sdl_backend.h` defines the versioned C boundary without exposing Deko handles.
  Borrowed images validate backend identity and dimensions and retain ownership
  across queued UI commands and submitted GPU frames.
- `borrowed_image.h` provides the shared lease. Native views use five persistent
  target versions (two draws per view, two UI frames in flight and one writer); outstanding UI readers and write fences prevent unsafe reuse.
- `image_array.h` owns configurable native image arrays with checked allocation
  arithmetic, layout alignment and zero initialization. GPU use must finish
  before reset; resources must be released before their device.
- Standalone clients call `verify_renderer_abi(local_renderer_abi())` before
  allocating the engine's renderer classes. Compile every client with the same
  Switch/platform definitions as the engine (`-D__SWITCH__`).
- SDL_FlushRenderer requires the driver's InvalidateCachedState callback. The
  adapter snapshots logical state; native UI recording rebinds its descriptors,
  shaders, colour writes and raster state independently of any scene pass.

## Diagnostic builds

Inside the existing devkitA64 container and mounts:

`bash tools/switch/build_deko_app.sh` builds the real menu/showroom app at
`build-switch-deko-app/gt3r-deko-app.nro`. Its profile and log are in the shared
Switch data folder `sdmc:/switch/gt3r/`, alongside the existing GLES install.
Native reflection textures use `NativeTexture`; no
Deko resource is represented as an SDL_GPUTexture. Per-model submission slots
retain sphere maps and copy their descriptors only after the slot fence.

```
GT3R_DEKO_VARIANT=frontend bash tools/switch/deko-sdl/build.sh
bash tools/switch/deko-sdl/build.sh
```

The frontend variant produces build-switch-deko-frontend/gt3r-deko-frontend.nro;
its log is sdmc:/switch/gt3r-deko-frontend.log. It loads a procedural cube through
the public factory, without GTDK/GTDS/ISO input. A changes viewport size; + exits.

The default race variant preserves build-switch-deko-sdl/gt3r-deko-sdl-probe.nro
and its existing assets/log. Use it for the previously validated six-car scene.

Native-only UI can submit intermediate batches under image-pool pressure.
`submission_frame.h` keeps one acquired swapchain image until presentation while
rotating command-memory slots independently. Queued readers are submitted before
waiting on their fences; waiting alone cannot retire a CPU-queued lease.

Static showroom models pack immutable textures into aligned slices of one GPU
image arena and one temporary upload arena. `model_allocation.h` checks packing
overflow and selects frontend frame-buffer budgets from the actual draw count;
race budgets stay unchanged. This reduces allocation work; its latency must be measured on hardware.

The application now uses `render/menu_car_load.*` to load native menu cars on a
worker. `background_model_upload()` is an explicit backend capability: Deko
serializes loaders on a dedicated upload queue created at startup, never sharing
the render queue across threads. Initial uploads complete before publication.
Only CPU-owned request data crosses into the job; SDL UI resources stay on the
owner thread. All registered jobs join before VFS/context destruction or VFS
mutations. One pending job per picker bounds work during rapid selection changes.

The developer `prizes` screen displays actual PRM presentations from the ISO.
Native item composition clears colour/depth once and preserves both for subsequent
items. Submission rings belong to models; borrowed-image fences protect the
complete view, including partial submissions on error. DrawItem resource overrides,
custom per-item light sets and frame-alpha effects are not yet
implemented; ordered object rendering does not implement race-wide blend sorting.

Derived models retain a texture family and borrow its immutable original GPU
images. Texture batches use `dynamic_model.h` for atomic validation/copying and
logical-to-physical tiny-image padding. Animated replacements are fenced per
consumer, not yet shared GPU-wide. Stable geometry layouts reuse vertex rings;
changed topology rebuilds geometry while retaining original images. Empty geometry
uses a clear-only submission. Topology changes/destruction conservatively retire
GPU reads before wrapper leases can disappear; frequent topology changes are not
yet a performance target. The `dynamic` screen exercises these APIs and independent
texture families using flags and a real car from the ISO.


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
