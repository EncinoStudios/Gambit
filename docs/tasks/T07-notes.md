# T07 notes (implementation report details)

Short version of the device model, shader workflow and golden tests: `docs/guides/Rendering.md`. Recipe for new
shaders: `.claude/skills/add-shader/SKILL.md`.

## Deviations from the brief

1. **Vulkan 1.3 is required** (loader and device), not "1.2+". NVRHI records dynamic rendering and synchronization2
   through their *core 1.3* entry points (`vkCmdBeginRendering`, `vkCmdPipelineBarrier2`) on the process-wide
   Vulkan-Hpp dispatcher. A 1.2 device would need those names aliased to the KHR functions per device, which a shared
   dispatcher cannot do safely when several devices exist (tests create several). NVRHI itself documents Vulkan 1.3;
   every current desktop driver and lavapipe have it. Shaders still target `--target-env vulkan1.2` (SPIR-V 1.5) as
   the brief says.
2. **The ShaderCache is keyed by name only**: the name carries the stage (`"PBR.frag"`), so name + stage is redundant;
   `Get(name, stage)` checks that the suffix matches and `Get(name)` derives the stage.
3. **Shaders of other targets**: `gb_add_shaders(<target> ...)` works for any target. Each gets a generated
   `Gambit::EmbeddedShaders::Get<Target>Shaders()`; the engine's are found by `ShaderLibrary::Find` directly, others are
   added with `ShaderLibrary::Register` (GambitTests does it in `main`). This keeps test shaders out of the engine.
4. **The Application owns the device** (`ApplicationSpecification::EnableGraphics` + `Graphics`), not the runtime alone:
   layers render between `BeginFrame` and `Present`, the editor (T20) and scene renderer (T08) reuse it. New standard
   argument `--no-gpu` (like Unreal's `-nullrhi`): no device, for machines without Vulkan and pure simulation runs.
5. **Runtime smoke tests**: `GambitRuntime.Headless` now runs `--headless --no-gpu` (simulation only, every platform);
   new `GambitRuntime.HeadlessRender` (offscreen) and the existing `GambitRuntime.Window` (Xvfb, swap chain) render and
   are *skipped* by CTest (`SKIP_REGULAR_EXPRESSION`) where no Vulkan device exists (Windows/macOS CI). All runtime
   smoke tests fail on any `[error]`/`[critical]` log line, so validation errors fail them too.
6. **Readback** covers more than RGBA8/RGBA16F/RGBA32F: R8/RG8/BGRA8 (+ sRGB) expand to RGBA8, R16F/RG16F to RGBA16F,
   R32F/RG32F to RGBA32F (missing channels 0, alpha 1), for T09's RG16F BRDF LUT and depth-as-R32F later. 3D and
   multisampled textures are rejected. `ReadbackBuffer` was added for compute results.
7. **`GB_UPDATE_GOLDEN=1` writes the golden and fails the test once**, asking for a review, like the DMath table (D-015):
   a CI job that accidentally keeps the variable set cannot go green with regenerated references.
8. **stb is compiled without stdio** (`STBI_NO_STDIO`, `STBI_WRITE_NO_STDIO` on `stb::stb`): images are decoded from
   and encoded to memory, so T08's importer has to read through the VFS (D-014). File helpers `SavePng`/`LoadPng` go
   through `FileSystem` and are meant for tools and tests.
9. glslang's own build generates a header with Python 3 (`find_package(Python3 REQUIRED)` in its StandAlone), so
   building needs Python 3; the embedding step is plain CMake as required. The compiler lands in `build/<Config>/tools/`.
10. `Image` lives in `RHI/Image.h` (readback returns it); PNG I/O and `ImageCompare` are free functions there.

## Required Vulkan features

Loader and device API 1.3; `timelineSemaphore` (1.2), `synchronization2` and `dynamicRendering` (1.3); one queue
family with graphics + compute (+ present to the window); `VK_KHR_swapchain` when windowed;
`VK_KHR_portability_subset` is enabled when offered (MoltenVK). Optional, enabled when present and reported in
`GraphicsDeviceInfo::Features`: geometryShader, samplerAnisotropy, imageCubeArray, fillModeNonSolid, depthClamp,
independentBlend, textureCompressionBC, shaderStorageImageWriteWithoutFormat, shaderDrawParameters. Instance:
`VK_EXT_debug_utils` and `VK_KHR_portability_enumeration` when available, the GLFW surface extensions when windowed.

## Design notes

- **Dispatchers.** `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` is in `VulkanGraphicsDevice.cpp`. NVRHI uses
  the default dispatcher; it is initialized at instance level only (device functions are then the loader's dispatching
  entry points, valid for every device) from the newest live instance, and re-initialized from another live instance
  when that one is destroyed. The device's own calls use a private dispatcher with device-level pointers.
  `VK_NO_PROTOTYPES` and `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1` are set once on `Vulkan::Headers` so NVRHI and the
  engine compile `vulkan.hpp` identically (otherwise an ODR violation).
- **Exceptions.** NVRHI builds `vulkan.hpp` with exceptions, so the engine must too. Setup code uses the enhanced API
  and `GraphicsDevice::Create` converts `vk::SystemError` into an `Error`; per-frame calls (acquire, present) use the
  non-throwing overloads. NVRHI's own waits and garbage collection throw `vk::DeviceLostError` on a lost device, so
  `BeginFrame`, `Present` and `WaitIdle` catch `vk::SystemError` and mark the device lost, and the readbacks return an
  `Error` (review fix).
- **Frames.** One NVRHI event query per frame slot throttles the CPU (`FramesInFlight`). Acquire semaphores are per frame
  slot (reused only after that slot's frame finished), present semaphores per swap-chain image (reused when the image is
  acquired again), the scheme recommended for swap-chain semaphore reuse. Waiting for the device does not cover the
  semaphore waits of earlier presentations (only `VK_EXT_swapchain_maintenance1` present fences would), so the present
  semaphores of a replaced swap chain are destroyed 8 frames later, not at the rebuild. `Present` submits one small
  command list that signals the present semaphore and moves an untouched image out of its undefined first-use layout.
- **Swap chain.** Rebuilt (after `WaitIdle`) when the window's framebuffer size differs from the size it was built for,
  when VSync changed, or after OUT_OF_DATE/SUBOPTIMAL; up to three acquire attempts per frame. VSync = FIFO, otherwise
  MAILBOX > IMMEDIATE > FIFO. A failed rebuild leaves no swap chain, so the next frame starts over. Images: min + 1, color attachment plus transfer src/dst when supported (readback,
  screenshots, clears). Headless back buffers rest in `ShaderResource`, swap-chain images in `Present`
  (`keepInitialState`), so both can be read back.
- **Validation errors** are counted per device: Vulkan debug-messenger messages of severity error and type validation,
  and NVRHI error/fatal messages. Loader errors (type general) are logged but not counted. Tests check the count per test
  (`GB_GPU_TEST`), and `main` fails the run when tearing the shared device down logs errors (leaked objects).
- **Conventions** (NVRHI on Vulkan): flipped viewport, so clip space +Y up, depth 0..1, framebuffer and UV origin
  top-left, clockwise front faces; `FullscreenTriangle.vert` follows them.

## Decisions needing confirmation

1. Vulkan 1.3 as the minimum (an addendum to D-030).
2. The Application owning the optional device, `--no-gpu`, and the runtime smoke tests that skip without Vulkan.
3. Golden policy: tolerance 2/255 per channel and at most 0.1 % bad pixels by default; `GB_UPDATE_GOLDEN` fails once.
4. stb compiled without stdio (all image IO through memory / the VFS).

## Verified

- Linux, GCC 13.3 and Clang 18.1, Debug and Release (and GCC Dist with `GB_USE_PCH=OFF`, as CI builds it): zero
  warnings, all 386 test cases and 9 smoke tests pass (31 new test cases: 13 gpu, 1 window, 17 CPU), including the
  window suite (Xvfb) on lavapipe (Mesa 25.2.8, LLVM 20.1.2, Vulkan 1.4.318) with the Khronos validation layer
  1.3.275: zero validation errors.
- Clean Debug build (GCC, `-j4`, ccache disabled, another build running on the 4-core machine): 4m25s; glslang and NVRHI
  are most of the added time.
- `GambitRuntime --headless --frames N` renders offscreen, `GambitRuntime --frames N` presents under Xvfb, and without a
  Vulkan driver (`VK_DRIVER_FILES=/nonexistent.json`) the render smoke tests and GPU tests skip with the reason.
- Editing `Gambit/Shaders/Include/ColorSpace.glsl` recompiles exactly the shaders that include it (depfiles), and a GLSL
  error fails the build with glslang's `ERROR: <file>:<line>` message.

## Not verified (no access here)

- Windows/MSVC and macOS/MoltenVK builds and devices; real GPUs. Likely touch points: glslang's depfile escapes ':' in
  Windows drive letters (Ninja's depfile parser may need a relative path there), `VK_USE_PLATFORM_WIN32_KHR` pulls
  `<windows.h>` into the Vulkan translation units (NOMINMAX/WIN32_LEAN_AND_MEAN are set), MoltenVK's Vulkan 1.3 support.
- Device-lost handling (logged, `IsDeviceLost`, the Application closes) cannot be provoked on lavapipe.

## Follow-ups for later tasks

- T08+: create pipelines per framebuffer format and never keep back-buffer handles across frames (a resize replaces
  them). Use `MakeBindingLayoutDesc` for every layout so that all layouts of a pipeline share the D-031 convention.
- Layers are attached before `Application::Initialize` creates the device, so they cannot make GPU resources in
  `OnAttach` (`RuntimeLayer` creates its command list in its first `OnRender`). T08/T20 may want layers attached after
  initialization, or a hook that runs once the device exists.
- `ReadbackTexture`/`ReadbackBuffer` create a staging resource, a command list and an event query per call and wait
  for the GPU: right for tests and screenshots; cache them if readback ever becomes per-frame work.
- Synchronization validation (`VK_LAYER_KHRONOS_validation` `validate_sync`) is not enabled; worth a CI job once the
  renderer has several passes.
