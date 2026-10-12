# Rendering guide: device, shaders, golden tests

Gambit renders through **NVRHI on Vulkan** (D-030). Engine code talks to the GPU with NVRHI handles
(`nvrhi::TextureHandle`, command lists, pipelines); Vulkan itself stays inside `Gambit/RHI/Vulkan/`.

## Device model (`RHI/GraphicsDevice.h`)

- `GraphicsDevice::Create(spec, window)` makes a windowed device (GLFW surface + swap chain) or, with
  `spec.Headless`, one with offscreen back buffers; the frame API is identical. `Application` owns one when
  `ApplicationSpecification::EnableGraphics` is set (the runtime does; `--no-gpu` turns it off) and calls layers'
  `OnRender` between `BeginFrame()` and `Present()`.
- Per frame: `if (device.BeginFrame()) { draw into device.GetCurrentFramebuffer(); device.Present(); }`. `BeginFrame`
  waits for the frame slot (`FramesInFlight`, default 2), recreates the swap chain on resize / VSync change / out of
  date, and returns false when nothing can be drawn (zero-sized window). Never keep back-buffer handles across frames.
- Requirements: Vulkan **1.3** (loader and device) with timeline semaphores, synchronization2 and dynamic rendering
  (NVRHI uses the core 1.3 entry points); one queue family for graphics + compute (+ present). GPU choice: preferred
  index/name, else discrete > integrated > virtual > CPU. Optional features (`GetInfo().Features`) are enabled when
  present — check before use. Lavapipe (Mesa) is the reference software device.
- Validation (`spec.EnableValidation`, default in Debug) = Khronos layer + NVRHI validation; messages go to the core log
  and errors are counted (`GetValidationErrorCount()`, must stay 0 in tests).
- Conventions: NVRHI flips the Vulkan viewport, so clip space is **+Y up** with depth 0..1 (GLM's zero-to-one
  matrices work unchanged), the framebuffer origin and UV (0, 0) are top-left, and front faces are **clockwise** on
  screen (NVRHI default). Textures read back with `ReadbackTexture` / `ReadbackBuffer` need `keepInitialState`.
- Back buffers: windowed 8-bit sRGB (or UNORM) BGRA/RGBA; headless exactly `spec.BackBufferFormat`
  (default `SRGBA8_UNORM`). Write linear values to sRGB targets; encode with `ColorSpace.glsl` for UNORM ones.

## Shader workflow (D-031)

1. Add `Gambit/Shaders/<Name>.<vert|frag|comp|geom>.glsl` (`#version 460`). Shared code goes in
   `Gambit/Shaders/Include/` and is included with `#extension GL_GOOGLE_include_directive : require` +
   `#include "File.glsl"`.
2. Bindings: `layout(set = S, binding = B)`, every `B` unique within a set across resource types; build layouts with
   `MakeBindingLayoutDesc(visibility, S, { ... })` (zero NVRHI binding offsets). Push constants:
   `layout(push_constant)` + `BindingLayoutItem::PushConstants`.
3. Build: CMake compiles every shader with the glslang built from `ThirdParty/` (`--target-env vulkan1.2`, `-g` in
   Debug, depfiles track includes); a compile error fails the build with glslang's message. The SPIR-V is embedded into
   the binary (`gb_add_shaders`), no loose files ship.
4. Use: `device.GetShaderCache().Get("<Name>.<stage>")` (stage from the suffix), or the helpers in
   `RHI/PipelineUtils.h`: `CreateComputePipeline`, `CreateFullscreenPipeline` + `DrawFullscreenTriangle` (the shared
   `FullscreenTriangle.vert` outputs `vec2 UV` at location 0). Test-only shaders live in `GambitTests/Shaders/`.

## Golden-image tests

- GPU tests are in doctest suite `gpu` and start with `GB_GPU_TEST(device)` (`GambitTests/src/TestUtils/GpuTest.h`):
  the shared headless device (validation on, 256×256), skipped with a message under `GB_TEST_NO_GPU=1` or without a
  usable Vulkan device, and a check that the test caused no validation errors.
- `Tests::CheckGoldenTexture(device, texture, "Name")` reads back and compares with `GambitTests/Golden/Name.png`
  (per-channel tolerance 2/255, ≤ 0.1 % bad pixels by default; float images are sRGB-encoded first). On mismatch the
  actual and diff images land in `build/test-output/`. `GB_UPDATE_GOLDEN=1` rewrites the golden and fails once:
  review the PNG, rerun without it, and say so in the commit message.
- Render goldens into a texture you create (fixed size, `keepInitialState`), not the shared back buffer; keep them small
  (≤ 256 px) and lavapipe-friendly. Numeric checks (readback + `CHECK`) beat goldens where values are known.
