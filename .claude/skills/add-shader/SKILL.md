---
name: add-shader
description: Add or change a GLSL shader of the Gambit engine (or a test-only shader), use it from C++ through the ShaderCache and pipeline helpers, and test it with numeric readback or a golden image. Use whenever a task adds a render or compute pass, edits Gambit/Shaders, adds a shared shader include, or needs a golden-image test.
---

# Add a shader

Shaders are GLSL 460 compiled to SPIR-V at build time and embedded in the binary (D-031). Background and conventions:
`docs/guides/Rendering.md`.

## 1. Write it

- File: `Gambit/Shaders/<Name>.<stage>.glsl`, stage `vert`, `frag`, `comp` or `geom`; PascalCase name, unique across
  the engine and the tests (`GambitTests/Shaders/` for test-only shaders). It is looked up as `"<Name>.<stage>"`.
- Shared code: `Gambit/Shaders/Include/<File>.glsl` with an include guard; use it with
  `#extension GL_GOOGLE_include_directive : require` and `#include "<File>.glsl"`. Existing: `ColorSpace.glsl`
  (`LinearToSrgb`, `SrgbToLinear`, matching the CPU functions in `RHI/Image.h`).
- Names follow AGENTS.md §2 (functions, structs and constants PascalCase: `DistributionGGX`, `MaxLights`; locals
  camelCase) with one prefix per interface: `a_` vertex attributes (`a_Position`), `v_` values passed between stages
  (`v_UV`, `v_Normal`), `o_` fragment outputs and buffers a shader writes (`o_Color`), `u_` uniforms, push constants,
  textures, samplers and read-only buffers (`u_Frame`, `u_BaseColorTexture`).
- Bindings: `layout(set = S, binding = B)` with `B` unique within the set across all resource types (NVRHI binding
  offsets are zero). Push constants: `layout(push_constant) uniform ...` (≤ 128 bytes to be portable).
- Conventions: clip space +Y up, depth 0..1, UV (0, 0) top-left, clockwise front faces (NVRHI default; scene meshes
  are counter-clockwise and use reversed depth, D-044). Post passes reuse `FullscreenTriangle.vert` and read
  `layout(location = 0) in vec2 v_UV`. Scene shaders include `SceneData.glsl` (frame constants, lights, draw push
  constants, mirrored by `Renderer/GpuTypes.h`) and `PBR.glsl` (BRDF).
- New files are picked up at the next configure (`build.py` re-globs automatically). A compile error fails the build
  with glslang's `ERROR: <file>:<line>: ...` message; the build log has the full command.

## 2. Use it

```cpp
ShaderCache& shaders = device.GetShaderCache();
Result<nvrhi::ShaderHandle> ps = shaders.Get("Tonemap.frag");               // stage from the suffix, cached

Result<nvrhi::BindingLayoutHandle> layout = CreateBindingLayout(device, MakeBindingLayoutDesc(
    nvrhi::ShaderType::Pixel, 0,
    { nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(1),
      nvrhi::BindingLayoutItem::PushConstants(2, sizeof(Params)) }));
const std::array layouts = { *layout };
Result<nvrhi::GraphicsPipelineHandle> pipeline =
    CreateFullscreenPipeline(device, "Tonemap.frag", framebuffer->getFramebufferInfo(), layouts);
nvrhi::GraphicsState state = MakeGraphicsState(*pipeline, framebuffer).addBindingSet(set);
DrawFullscreenTriangle(*commandList, state, std::as_bytes(std::span(&params, 1)));
// Compute: CreateComputePipeline(device, "Blur.comp", layouts), then setComputeState / dispatch.
```
Handle every `Result` (NVRHI logs why it returned null). Create pipelines once (per framebuffer format), not per frame.

## 3. Test it

In `GambitTests/src/<Module>/`, suite `gpu`, first line `GB_GPU_TEST(device);` (`TestUtils/GpuTest.h`):
- Prefer numeric checks: render into a small texture you create (`keepInitialState = true`), then
  `device.ReadbackTexture(texture)` / `device.ReadbackBuffer(buffer, offset, size)` and compare values.
- Images: `Tests::CheckGoldenTexture(device, texture, "<Name>")` (`TestUtils/Golden.h`). Create or update the golden
  with `GB_UPDATE_GOLDEN=1 build/Debug/bin/GambitTests --test-case="<test>"` (fails once on purpose), look at
  `GambitTests/Golden/<Name>.png`, rerun without the variable, and mention the regenerated golden in the commit.
- Mismatches leave `<Name>.actual.png` and `<Name>.diff.png` in `build/test-output/`.
- The test must end with zero validation errors (`GB_GPU_TEST` checks it); release every NVRHI object it creates.

## 4. Finish

`python3 scripts/format.py`, then `python3 scripts/check.py` (and with `CC=clang CXX=clang++`). Update
`docs/guides/Rendering.md` when you add a convention or a shared include.
