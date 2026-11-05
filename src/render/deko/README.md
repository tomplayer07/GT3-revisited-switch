# Native Switch renderer integration

The SDL 2D driver and its versioned C callback contract live here so the
application and diagnostic programs can share one implementation.

- `SDL_render_deko.c` is built into the isolated patched SDL archive, not
  into the engine: it uses SDL private headers from the matching source build.
- `sdl_backend.h` is the public boundary. The renderer borrows the backend;
  its owner must outlive the renderer and all its textures.
- `tools/switch/deko-sdl/prepare_sdl.py` verifies the base SDL object against
  the source build, preserves the AUDOUT fix and records driver/contract hashes.
- The old tools headers/source are compatibility entry points only.

This extraction does not yet provide a generic native 3D backend. Device,
resource and swapchain ownership still reside in the probe graphics module.
The legacy borrowed-scene property still addresses one fixed-size diagnostic image.
The new borrowed-image property accepts an explicit image contract with backend
identity, dimensions, retain/release callbacks and per-frame descriptor lookup.
Queued commands and in-flight frames retain it independently of SDL texture
lifetime. The demo exercises this new contract; its provider still exports the
existing scene images. Arbitrary view allocation is not implemented yet.
Generalizing that property, moving device/resource ownership here and routing
GpuContext/GpuModel/View3D to it are the next integration steps.

Do not select Deko for the full application until those routes exist. GLES
remains the application's implementation during this migration.

`image_array.h` owns a configurable array of native images with checked
allocation arithmetic and zero initialization. The probe uses it for swapchain,
scene and reversed-depth images. Reset requires GPU completion; the context
waits before releasing these arrays and destroys the device last. This is the
allocation primitive, not a complete View3D implementation or resize scheduler.

Production GpuModel and the native model uploader now share CPU geometry
preparation in render/model_geometry.*. Native upload requests packet-order
preservation; production retains its existing merge policy. This moves the
CPU-side metadata out of the SDL_GPU-dependent class without pretending
that native buffer/texture ownership is already routed through GpuContext.

The game frontend now exposes `render/native_renderer.h`: GpuContext can adopt
a native backend, GpuModel dispatches typed model resources and View3D::draw
composites a backend-owned picture. Host tests use a synthetic software provider
to verify this boundary. A concrete Deko NativeRenderer adapter, native texture
bridge and application startup selection are NOT implemented yet.

A first concrete NativeRenderer adapter now exists in the probe graphics module.
It implements ordinary static model upload and one basic offscreen model view
using three fenced target versions and persistent borrowed SDL wrappers. The
A overlay exercises GpuContext/GpuModel/View3D with a rotating procedural cube.
Earlier statements that no concrete adapter exists describe the prior step.
Full-game startup, dynamic models and full DrawOptions/texture parity remain
unimplemented; the provider still borrows the probe's scene-initialized device.
