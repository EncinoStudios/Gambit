# T08 notes (implementation report details)

How the renderer, its assets and its tests fit together: `docs/guides/Rendering.md` ("Scene rendering"); file formats
(`.meta` sub-assets, texture import settings, `.gmat`, glTF): `docs/guides/FileFormats.md`. Decisions: D-037 (MikkTSpace),
D-038 (sub-assets), D-039 (built-in assets), D-044 (renderer conventions), D-045 (glTF import scope).

## Deviations from the brief

1. **Decision IDs.** D-040..D-043 already exist (physics, scripting, animation, audio), so the new entries use D-037,
   D-038, D-039, D-044 and D-045 of the reserved range.
2. **`.hdr` files stay `Environment` assets** (T05's mapping; T09 imports them as environments). The texture decoder
   handles Radiance HDR (`DecodeTextureImage` gives RGBA32F, `Texture2D` keeps float images linear and uploads them as
   RGBA16F), so T09 builds on it; the texture importer itself loads `.png .jpg .jpeg .tga .bmp`.
3. **`.glb`/`.gltf` are `Model` assets**, a new `AssetType` (T05 mapped them to `Mesh`); meshes are sub-assets.
4. **Mip generation uses render passes** (a fullscreen box filter per level, `GenerateMips.frag`) rather than compute:
   sRGB formats cannot be storage images, and rendering encodes sRGB in hardware. Odd sizes use the overlap weights of
   the three covered texels, so non-power-of-two textures do not shift.
5. **Points and lines in glTF are skipped** with a warning (they cannot become triangles); a mesh without triangles is
   not imported.
6. **One UV transform per material** (`UVTiling`/`UVOffset`, as the brief's material has), taken from the first textured
   slot's `KHR_texture_transform`; rotation and `TEXCOORD_1` are warnings (D-045).
7. **Spot light intensity**: lumens converted like a point light (`/ 4 pi`), so narrowing the cone does not brighten
   it; the component doc says so.
8. **No registry commands** (`model.instantiate`, `asset.import`), per the brief's note: `ModelUtils::Instantiate` and
   the importers are the C++ API T22 wires.
9. **Frame-time test** "Renderer: the sphere grid renders at 1280x720 (frame time)" is in suite `slow` (a doctest test
   case has one suite); `GB_GPU_TEST` still skips it without a device.

## Design notes

- **Sub-assets (D-038).** `SubAssetHandles` hands out handles by key during an import (known keys keep theirs); the
  `AssetManager` writes new keys to the `.meta` after a load and registers and caches the sub-assets
  (`Asset::GetSubAssets`). A scan registers listed sub-assets before anything loads, so references resolve at once.
  `AssignNewHandle` renews the sub-asset handles of a copied file too.
- **Built-in assets (D-039)** are registered per `AssetManager` (`RegisterEngineAssets`, one function per module like
  `RegisterEngineTypes`), created on first load and kept across scans.
- **Extraction** keeps the previous world matrix per entity UUID (lookups only) and owns the world's
  `DebugDrawList`; both are presentation state outside the simulation hash.
- **RenderResources** keys GPU copies by handle, re-checks each once per frame (`BeginFrame`) against the object the
  `AssetManager` hands out (weak pointer) and the material version, and reports failures once.
- **SceneRenderer** records one command list per `Render`; the overlay framebuffer (target color + view depth) is made
  per call because targets such as back buffers must not be kept across frames (T07 follow-up). Pipelines are cached per
  framebuffer kind; mesh pipelines are made for the fixed view-target formats and refuse other framebuffers. The depth
  prepass has an input layout of its own (position, UV0, color), so it fetches no attributes it does not read and the
  validation layer reports no performance warnings either.
- **Normal maps** are applied only when a material has one (a flag in the material constants), so materials without
  one shade with the exact interpolated normal rather than the 8-bit flat-normal texture.
- **T07 follow-ups**: `VulkanLoader` loads the Vulkan loader once for the device and GLFW (`glfwInitVulkanLoader`, needed
  on macOS); `ErrorBurstLimiter` logs repeated acquire/present/rebuild failures (a lost surface) once per burst and a
  summary when they stop; shader commands run in the binary directory with relative paths, so glslang's depfiles hold
  no drive letters (`touch Include/SceneData.glsl` recompiles exactly its 4 users, a no-op build does nothing).

## Verified

- Linux, GCC 13.3 and Clang 18.1, Debug (and GCC Release): zero warnings; all tests pass, the GPU ones on lavapipe
  (Mesa, LLVM 20.1.2) with the Khronos validation layer: zero validation errors.
- Frame time of the 5 x 5 sphere grid (two lights) at 1280x720 on lavapipe (llvmpipe, LLVM 20.1.2, 4 CPUs shared with
  other builds), CPU + GPU of one frame with nothing overlapping, measured with the slow test above: about 20 ms in
  Release (GCC 13.3), 31..48 ms in Debug (GCC 13.3, Clang 18.1).

## Not verified (no access here)

- Windows/MSVC and macOS/MoltenVK, real GPUs (golden images were made on lavapipe; tolerances as D-036).
- The glTF importer against the Khronos sample models (no network); the generated test files cover the features listed
  in `tools/gen_test_gltf.py`.

## Follow-ups for later tasks

- T09: replace `TonemapPass` and the uniform ambient (`RenderEnvironment`, `EnvironmentBRDFApprox` in `PBR.glsl`) with
  the post chain and IBL; `ProjectSettings::Rendering` (Tonemapper, Exposure) is not read by the renderer yet.
- T10: the depth prepass writes no normals yet; `CastShadows`/`ReceiveShadows` are extracted but unused.
- T11: bind `Debug.DrawLine/DrawBox/DrawSphere` to `RenderExtractionSystem::GetDebugDraw()` (`DebugDrawList`).
- T14: skins and animations of glTF files (the importer warns), joints/weights are imported but not uploaded.
- T22: commands `model.instantiate` and `asset.import` for models; the editor picks a mesh *or* model for
  `MeshRenderer.Mesh` (its reflection attribute names `Mesh`).
