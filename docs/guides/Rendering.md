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
  screen (NVRHI default); scene meshes are counter-clockwise (glTF, D-044), so their pipelines set
  `frontCounterClockwise`. Textures read back with `ReadbackTexture` / `ReadbackBuffer` need `keepInitialState`.
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

## Scene rendering (T08)

Data flows one way (D-032, D-044): components → `RenderExtractionSystem` (PreRender phase, also in edit ticks) →
`RenderScene` → `SceneRenderer::Render(scene, view, alpha, target)`. The renderer never reads the World.

- **Components** (`Renderer/Components/RenderComponents.h`, reflected): `MeshRenderer` (a Mesh or a whole Model,
  `SubmeshIndex` -1 = all, `MaterialOverrides` per slot, `CastShadows`, `ReceiveShadows`, `Visible`; `SubmeshIndex` and
  `MaterialOverrides` apply to Mesh assets only, a Model ignores them with a warning), `Camera`
  (perspective/orthographic, `VerticalFOV` degrees, `Near`, `Far`, `OrthoSize` = view height, `Primary`, `ClearColor`),
  `DirectionalLight` (`Illuminance` lux), `PointLight` and `SpotLight` (`Intensity` lumens, `Range` 0 = unlimited, cone
  angles in degrees). Cameras look and lights shine along the entity's **-Z**; +Y is up.
- **Extraction** copies active entities with a `WorldTransform` (add the `TransformSystem`) into lists sorted by UUID,
  each with its previous- and current-tick world matrix (`RenderTransform::Interpolate(alpha)`: lerp, shortest-arc
  slerp), and the world's simulated time of both ticks (`RenderScene::GetTime(alpha)`, the shaders' `u_Frame.Time`;
  it stands still in edit ticks). `Snap(entity)` drops one entity's previous transform (a teleport or respawn shows no
  motion) and `ResetHistory()` all of them and the previous time (after loading a scene). Its `DebugDrawList`
  (`GetDebugDraw()`) collects debug lines with durations in seconds of ticks; it is presentation state, never
  simulation state. `SetEnvironment` sets the uniform ambient light (until T09's IBL).
- **A view** (`RenderView`): an id (`ViewId`), view and projection matrices (`MakeProjection`: reversed depth), clear
  color, `ExposureEV100`, debug options (`DrawDebugLines`, `Shapes`: bounds, lights, cameras).
  `RenderView::FromCamera(camera, alpha, aspect)` builds one from a scene camera (its id is the camera entity's UUID);
  the editor builds its own and gives each viewport an id of its own.

```cpp
SceneRenderer renderer(device, assets);                               // once; owns RenderResources and the passes
const RenderScene& scene = extraction.GetRenderScene();
if (const RenderCamera* camera = scene.FindPrimaryCamera(); camera != nullptr && device.BeginFrame())
{
    const glm::uvec2 size = device.GetBackBufferSize();
    RenderView view = RenderView::FromCamera(*camera, alpha, float(size.x) / float(size.y));
    Result<SceneRenderStats> stats = renderer.Render(scene, view, alpha, device.GetCurrentFramebuffer());
    device.Present();
}
```

**Passes** (each records into the renderer's command list with explicit inputs and outputs, `Renderer/Passes/`):
`DepthPrepass` (opaque and masked draws → D32 depth, cleared to 0) → `ForwardPass` (opaque/masked against the prepass's
depth with `GreaterOrEqual`, then blended draws back to front → `RGBA16F` scene color, pre-exposed) → `TonemapPass`
(temporary fitted ACES, sRGB-encoded for UNORM targets; T09 replaces it) → `DebugLinePass` (depth-tested lines, then
overlay lines, into the target with the view's depth). Each view id has view targets of its own (HDR scene color and
depth, `FindViewTargets(viewId)`), sized like its target and replaced when that size changes; the targets of views not
rendered for `TargetIdleRenders` renders are released, so an editor viewport and a game view can render in one frame.
Targets are any single-sampled framebuffer with one color attachment and no depth attachment (back buffers included;
nothing is kept across calls).

**Draw lists**: models draw every node's mesh at `world * node.ModelTransform` with the meshes' own materials;
materials are resolved (override, mesh slot, default material) for every submesh, then submeshes are culled against the
view frustum (`Frustum`, `AABB::Transformed`), opaque/masked
draws sorted by pipeline, material and mesh handle, blended ones by view depth. Lights: directional first, then spot and
point lights whose range sphere touches the frustum, at most `MaxLights` (256), in a structured buffer.

**GPU data** (`Renderer/GpuTypes.h` mirrors `Shaders/Include/SceneData.glsl`): set 0 = frame constants (binding 0),
lights (1), draw push constants (2: model matrix, normal matrix columns with the determinant's sign in `[0].w`); set 1 =
material constants (0) and five texture/sampler pairs (1..10: base color, metallic-roughness, normal, occlusion,
emissive). Vertex streams: positions alone (stream 0), then normal, tangent, UV0, UV1, color interleaved (stream 1).

**Shading** (`PBR.frag`, `Include/PBR.glsl`): glTF metallic-roughness, GGX + height-correlated Smith + Schlick +
Lambert (Filament's formulation, roughness clamped to 0.045); normal maps in MikkTSpace (D-037) with `NormalScale` (the
vertex shader normalizes the transformed normal and tangent, so scaled draws shade like unscaled ones);
occlusion scales the ambient light only; emissive color × strength is a luminance (cd/m², pre-exposed like everything
else). Alpha: `Opaque` ignores alpha, `Mask` discards below `AlphaCutoff` in both passes, `Blend` blends over
what is behind (`SrcAlpha`, `1 - SrcAlpha`) without depth writes. Double-sided materials
draw back faces with the whole tangent frame flipped. Point and spot lights fall off with the inverse square, windowed to
zero at `Range`; spot cones fade between the inner and outer angle.

### Assets the renderer draws

| Asset | Source | Notes |
|---|---|---|
| `Texture2D` | `.png` `.jpg` `.tga` `.bmp` (stb_image, through the VFS), glTF images | RGBA8 (sRGB or linear per `TextureImportSettings`), mips generated on the GPU (`GenerateMips.frag`, box filter in linear space, odd sizes weighted); HDR data decodes to RGBA32F (`.hdr` files are `Environment` assets, T09) |
| `MeshSource` | glTF meshes, `MeshFactory` | CPU `MeshData` (streams, 32-bit indices, submeshes with material slots, bounds); uploaded as `GpuMesh` |
| `Material` | `.gmat`, glTF materials | reflected struct `Material` (see FileFormats.md); `SetData` bumps the version and the GPU copy follows |
| `Model` | `.gltf` / `.glb` (cgltf) | node hierarchy + sub-assets (D-038); `ModelUtils::Instantiate` creates entities |

`RenderResources` creates GPU copies on first use and checks them once per frame: a reloaded asset or a new material
version is re-uploaded; assets that fail to load or upload are reported once and replaced (default textures, the
default material, no mesh), and a failed upload is retried only for another object or version of the asset. Each copy
holds its asset, so `AssetManager::UnloadUnused` keeps what is drawn; copies no draw asked for during
`IdleFramesBeforeRelease` frames (renders) are released. The renderer asks for the materials of culled submeshes too, so
what a scene uses stays on the GPU while it is out of view. Built-in assets have fixed handles (D-039):
`MeshFactory::GetHandle(PrimitiveMesh::Cube)`, `GetDefaultTextureHandle(DefaultTexture::Checker)`,
`Material::DefaultHandle`.

Golden tests of the renderer (`GambitTests/src/Renderer/SceneRendererTests.cpp`): a metallic × roughness sphere grid,
a textured glTF model rendered through a World, alpha mask/blend ordering, debug lines; numeric tests check the BRDF
luminance of a lit plane (directional, point and spot lights, the metallic-roughness texture), emissive output, normal
maps at any draw scale, mip filtering, float targets and mirrored winding. Test models come from
`tools/gen_test_gltf.py` (output committed under `GambitTests/Assets/Models/`).
