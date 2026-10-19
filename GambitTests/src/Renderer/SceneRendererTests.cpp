#include <doctest/doctest.h>

#include "Renderer/RendererTestSupport.h"
#include "TestSupport.h"
#include "TestUtils/Golden.h"
#include "TestUtils/GpuTest.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/ECS/World.h"
#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/Image.h"
#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/MeshFactory.h"
#include "Gambit/Renderer/Passes/TonemapPass.h"
#include "Gambit/Renderer/RenderResources.h"
#include "Gambit/Renderer/SceneRenderer.h"
#include "Gambit/Renderer/Systems/RenderExtractionSystem.h"
#include "Gambit/Renderer/Texture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	constexpr float Pi = std::numbers::pi_v<float>;

	nvrhi::TextureHandle CreateTarget(GraphicsDevice& device, uint32_t width, uint32_t height, nvrhi::Format format)
	{
		nvrhi::TextureHandle texture =
			device.GetNvrhiDevice()->createTexture(nvrhi::TextureDesc()
		                                               .setWidth(width)
		                                               .setHeight(height)
		                                               .setFormat(format)
		                                               .setIsRenderTarget(true)
		                                               .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                               .setKeepInitialState(true)
		                                               .setDebugName("Test render target"));
		REQUIRE(texture != nullptr);
		return texture;
	}

	Image ReadBack(GraphicsDevice& device, nvrhi::ITexture* texture, uint32_t mipLevel = 0)
	{
		Result<Image> image = device.ReadbackTexture(texture, mipLevel);
		const std::string problem = image.HasValue() ? std::string() : image.GetError().Message;
		REQUIRE_MESSAGE(image.HasValue(), problem);
		return std::move(*image);
	}

	// A project, a scene renderer on the shared device and a target to render into.
	struct RenderFixture
	{
		RendererProject Project;
		SceneRenderer Renderer;
		nvrhi::TextureHandle Target;
		nvrhi::FramebufferHandle Framebuffer;
		uint64_t NextHandle = 0x7000;

		RenderFixture(
			GraphicsDevice& device, uint32_t width, uint32_t height, nvrhi::Format format = nvrhi::Format::SRGBA8_UNORM)
			: Renderer(device, Project.Assets), Target(CreateTarget(device, width, height, format))
		{
			Framebuffer =
				device.GetNvrhiDevice()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(Target));
			REQUIRE(Framebuffer != nullptr);
		}

		[[nodiscard]] float GetAspectRatio() const
		{
			return static_cast<float>(Target->getDesc().width) / static_cast<float>(Target->getDesc().height);
		}

		// A material (or texture) for this test only: a built-in asset with a handle of its own.
		AssetHandle AddMaterial(const MaterialData& data)
		{
			const AssetHandle handle(NextHandle++);
			Project.Assets.RegisterBuiltinAsset(
				handle, AssetType::Material, std::format("Test/{}", handle.ToString()),
				[data] { return Ref<Asset>(CreateRef<Material>(data)); });
			return handle;
		}

		AssetHandle AddTexture(const Image& image, const TextureImportSettings& settings)
		{
			const AssetHandle handle(NextHandle++);
			Project.Assets.RegisterBuiltinAsset(
				handle, AssetType::Texture, std::format("Test/{}", handle.ToString()),
				[image, settings] { return Ref<Asset>(CreateRef<Texture2D>(image, settings)); });
			return handle;
		}

		SceneRenderStats Render(const RenderScene& scene, const RenderView& view, float alpha = 1.0f)
		{
			Result<SceneRenderStats> stats = Renderer.Render(scene, view, alpha, Framebuffer);
			const std::string problem = stats.HasValue() ? std::string() : stats.GetError().Message;
			REQUIRE_MESSAGE(stats.HasValue(), problem);
			return *stats;
		}
	};

	// The shared device with a smaller texture size limit: forwards everything else.
	class LimitedGraphicsDevice final : public GraphicsDevice
	{
	public:
		LimitedGraphicsDevice(GraphicsDevice& device, uint32_t maxTextureDimension)
			: m_Device(device), m_Info(device.GetInfo())
		{
			m_Info.Limits.MaxTextureDimension2D = maxTextureDimension;
		}

		[[nodiscard]] bool BeginFrame() override { return m_Device.BeginFrame(); }
		bool Present() override { return m_Device.Present(); }
		[[nodiscard]] nvrhi::ITexture* GetCurrentBackBuffer() const override { return m_Device.GetCurrentBackBuffer(); }
		[[nodiscard]] nvrhi::IFramebuffer* GetCurrentFramebuffer() const override
		{
			return m_Device.GetCurrentFramebuffer();
		}
		[[nodiscard]] uint32_t GetCurrentBackBufferIndex() const override
		{
			return m_Device.GetCurrentBackBufferIndex();
		}
		[[nodiscard]] uint32_t GetBackBufferCount() const override { return m_Device.GetBackBufferCount(); }
		[[nodiscard]] glm::uvec2 GetBackBufferSize() const override { return m_Device.GetBackBufferSize(); }
		[[nodiscard]] nvrhi::Format GetBackBufferFormat() const override { return m_Device.GetBackBufferFormat(); }
		[[nodiscard]] uint64_t GetFrameCount() const override { return m_Device.GetFrameCount(); }
		[[nodiscard]] bool IsInFrame() const override { return m_Device.IsInFrame(); }
		[[nodiscard]] Result<void> ResizeBackBuffers(uint32_t width, uint32_t height) override
		{
			return m_Device.ResizeBackBuffers(width, height);
		}
		[[nodiscard]] bool IsVSync() const override { return m_Device.IsVSync(); }
		void SetVSync(bool enabled) override { m_Device.SetVSync(enabled); }
		void WaitIdle() override { m_Device.WaitIdle(); }
		[[nodiscard]] nvrhi::IDevice* GetNvrhiDevice() const override { return m_Device.GetNvrhiDevice(); }
		[[nodiscard]] const GraphicsDeviceInfo& GetInfo() const override { return m_Info; }
		[[nodiscard]] bool IsHeadless() const override { return m_Device.IsHeadless(); }
		[[nodiscard]] bool IsDeviceLost() const override { return m_Device.IsDeviceLost(); }
		[[nodiscard]] uint64_t GetValidationErrorCount() const override { return m_Device.GetValidationErrorCount(); }
		[[nodiscard]] ShaderCache& GetShaderCache() override { return m_Device.GetShaderCache(); }

	private:
		GraphicsDevice& m_Device;
		GraphicsDeviceInfo m_Info;
	};

	RenderDrawItem MakeDraw(AssetHandle mesh, const glm::mat4& transform, std::vector<AssetHandle> overrides = {})
	{
		RenderDrawItem item;
		item.Mesh = mesh;
		item.Transform.Previous = transform;
		item.Transform.Current = transform;
		item.MaterialOverrides = std::move(overrides);
		return item;
	}

	glm::mat4 Place(const glm::vec3& translation, float scale = 1.0f, const glm::quat& rotation = glm::quat(1, 0, 0, 0))
	{
		return ComposeTransform(translation, rotation, glm::vec3(scale));
	}

	// A light (or camera) whose -Z axis points along `direction`.
	glm::mat4 Facing(const glm::vec3& position, const glm::vec3& direction)
	{
		const glm::vec3 forward = glm::normalize(direction);
		const glm::vec3 up = std::abs(forward.y) > 0.99f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		return glm::inverse(glm::lookAtRH(position, position + forward, up));
	}

	RenderView MakeView(const glm::vec3& eye, const glm::vec3& target, const CameraComponent& camera, float aspect)
	{
		RenderView view;
		const glm::vec3 forward = glm::normalize(target - eye);
		const glm::vec3 up = std::abs(forward.y) > 0.99f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		view.View = glm::lookAtRH(eye, target, up);
		view.Projection = MakeProjection(camera, aspect);
		view.ClearColor = camera.ClearColor;
		view.ExposureEV100 = 0.0f;
		return view;
	}

	RenderDirectionalLight MakeSun(const glm::vec3& direction, float illuminance, const glm::vec3& color = glm::vec3(1))
	{
		RenderDirectionalLight sun;
		sun.Transform.Previous = sun.Transform.Current = Facing(glm::vec3(0.0f), direction);
		sun.Light.Illuminance = illuminance;
		sun.Light.Color = color;
		return sun;
	}

	// The fitted ACES curve of the temporary tonemap pass (Tonemap.frag).
	float TonemapAces(float x)
	{
		return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
	}

	// The sphere grid of the PBR golden: metallic grows downwards, roughness to the right.
	RenderScene MakeSphereGrid(RenderFixture& fixture)
	{
		RenderScene scene;
		const AssetHandle sphere = MeshFactory::GetHandle(PrimitiveMesh::Sphere);
		for (int row = 0; row < 5; ++row)
		{
			for (int column = 0; column < 5; ++column)
			{
				MaterialData material;
				material.BaseColor = glm::vec4(0.9f, 0.55f, 0.25f, 1.0f);
				material.Metallic = static_cast<float>(row) / 4.0f;
				material.Roughness = static_cast<float>(column) / 4.0f;
				const glm::vec3 position(static_cast<float>(column) - 2.0f, 2.0f - static_cast<float>(row), 0.0f);
				scene.DrawItems.push_back(MakeDraw(sphere, Place(position, 0.9f), { fixture.AddMaterial(material) }));
			}
		}
		scene.DirectionalLights.push_back(MakeSun(glm::vec3(-1.0f, -1.0f, -1.0f), 1.8f));
		RenderPointLight point;
		point.Light.Color = glm::vec3(0.5f, 0.7f, 1.0f);
		point.Light.Intensity = 120.0f;
		point.Light.Range = 0.0f;
		point.Transform.Previous = point.Transform.Current = Place(glm::vec3(1.5f, 1.5f, 2.0f));
		scene.PointLights.push_back(point);
		scene.Environment.AmbientLuminance = 0.1f;
		return scene;
	}

	RenderView MakeSphereGridView(float aspect)
	{
		CameraComponent camera;
		camera.Projection = ProjectionType::Orthographic;
		camera.OrthoSize = 5.2f;
		camera.Near = 1.0f;
		camera.Far = 20.0f;
		camera.ClearColor = glm::vec4(0.02f, 0.02f, 0.03f, 1.0f);
		return MakeView(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), camera, aspect);
	}
}

TEST_CASE("Renderer: mip chains are box-filtered on the GPU in linear space" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RendererProject project;
	RenderResources resources(device, project.Assets);

	// sRGB: black and white columns average to linear 0.5, which is stored sRGB-encoded.
	Image stripes(4, 4, ImageFormat::RGBA8);
	for (uint32_t y = 0; y < 4; ++y)
	{
		for (uint32_t x = 0; x < 4; ++x)
		{
			stripes.SetPixel(x, y, glm::vec4(x % 2 == 0 ? 0.0f : 1.0f, 0.0f, 0.0f, 1.0f));
		}
	}
	const Result<GpuTexture> srgb = resources.CreateTexture(Texture2D(stripes, TextureImportSettings{}), "Stripes");
	REQUIRE(srgb.HasValue());
	CHECK(srgb->MipLevels == 3);
	CHECK(srgb->Texture->getDesc().format == nvrhi::Format::SRGBA8_UNORM);
	const uint8_t half = static_cast<uint8_t>(std::lround(LinearToSrgb(0.5f) * 255.0f));
	for (uint32_t level = 1; level < 3; ++level)
	{
		const Image mip = ReadBack(device, srgb->Texture, level);
		CHECK(mip.GetWidth() == 4u >> level);
		const glm::vec4 texel = mip.GetPixel(0, 0);
		CHECK(std::abs(static_cast<int>(std::lround(texel.r * 255.0f)) - half) <= 1);
		CHECK(texel.a == 1.0f);
	}
	CHECK(ReadBack(device, srgb->Texture, 0) == stripes); // level 0 is the image as it was

	// Odd sizes: a 3 x 1 linear image becomes one texel weighting each source texel by its overlap (1/3 each).
	Image odd(3, 1, ImageFormat::RGBA8);
	odd.SetPixel(0, 0, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	odd.SetPixel(1, 0, glm::vec4(0.6f, 0.3f, 0.0f, 1.0f));
	odd.SetPixel(2, 0, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
	TextureImportSettings linear;
	linear.Srgb = false;
	const Result<GpuTexture> oddTexture = resources.CreateTexture(Texture2D(odd, linear), "Odd");
	REQUIRE(oddTexture.HasValue());
	REQUIRE(oddTexture->MipLevels == 2);
	const glm::vec4 averaged = ReadBack(device, oddTexture->Texture, 1).GetPixel(0, 0);
	CHECK(averaged.r == doctest::Approx((0.6f + 0.9f) / 3.0f).epsilon(0.01));
	CHECK(averaged.g == doctest::Approx((0.3f + 0.9f) / 3.0f).epsilon(0.01));

	// Without mips: one level, no render target needed.
	TextureImportSettings single;
	single.GenerateMips = false;
	const Result<GpuTexture> flat = resources.CreateTexture(Texture2D(stripes, single), "Flat");
	REQUIRE(flat.HasValue());
	CHECK(flat->MipLevels == 1);
}

TEST_CASE("Renderer: GPU copies of assets are cached and replaced when the asset changes" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RendererProject project;
	project.Directory.WriteFile("Assets/Corrupt.png", "no PNG");
	project.Assets.Scan();
	RenderResources resources(device, project.Assets);

	resources.BeginFrame();
	const GpuMesh* cube = resources.GetMesh(MeshFactory::GetHandle(PrimitiveMesh::Cube));
	REQUIRE(cube != nullptr);
	CHECK(cube->VertexCount == 24);
	CHECK(cube->IndexCount == 36);
	CHECK(resources.GetMesh(MeshFactory::GetHandle(PrimitiveMesh::Cube)) == cube);

	// Failures are reported once and replaced: textures by a default texture, materials by the default material,
	// meshes by nothing.
	const ScopedLogCapture log;
	const AssetHandle corrupt = project.Assets.GetHandle("Assets/Corrupt.png");
	const GpuTexture& fallback = resources.GetTexture(corrupt, DefaultTexture::FlatNormal);
	CHECK(&fallback == &resources.GetTexture(AssetHandle(), DefaultTexture::FlatNormal));
	CHECK(resources.GetMesh(UUID(0xDEAD)) == nullptr);
	CHECK(resources.GetMesh(Material::DefaultHandle) == nullptr); // not a mesh
	const GpuMaterial& defaultMaterial = resources.GetMaterial(AssetHandle());
	CHECK(&resources.GetMaterial(UUID(0xBEEF)) == &defaultMaterial);
	resources.BeginFrame();
	CHECK(resources.GetMesh(UUID(0xDEAD)) == nullptr);
	const std::vector<LogEntry> entries = log.GetEntries();
	CHECK(
		std::ranges::count_if(
			entries,
			[](const LogEntry& entry) {
				return entry.Level == LogLevel::Error && entry.Message.find("0x000000000000DEAD") != std::string::npos;
			})
		== 1);

	// Editing a material rebuilds its GPU copy at the next frame.
	CHECK(resources.GetMaterial(Material::DefaultHandle).Alpha == AlphaMode::Opaque);
	const Ref<Material> material = project.Assets.GetAsset<Material>(Material::DefaultHandle);
	REQUIRE(material != nullptr);
	MaterialData edited = material->GetData();
	edited.Alpha = AlphaMode::Blend;
	material->SetData(edited);
	CHECK(resources.GetMaterial(Material::DefaultHandle).Alpha == AlphaMode::Opaque); // checked this frame already
	resources.BeginFrame();
	CHECK(resources.GetMaterial(Material::DefaultHandle).Alpha == AlphaMode::Blend);
}

TEST_CASE("Renderer: GPU copies are rebuilt when an asset is reloaded" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RendererProject project;
	const auto writePng = [&](uint32_t size) {
		Image image(size, size, ImageFormat::RGBA8);
		image.SetPixel(0, 0, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
		std::filesystem::create_directories(project.Directory.Resolve("Assets"));
		REQUIRE(SavePng(image, project.Directory.Resolve("Assets/Paint.png")).HasValue());
	};
	writePng(4);
	project.CopyTexturedCube();
	project.Assets.Scan();
	const AssetHandle paint = project.Assets.GetHandle("Assets/Paint.png");
	MaterialData painted;
	painted.BaseColorTexture = paint;
	const AssetHandle material(0x7100);
	project.Assets.RegisterBuiltinAsset(
		material, AssetType::Material, "Test/Painted", [painted] { return Ref<Asset>(CreateRef<Material>(painted)); });
	const AssetHandle model = project.Assets.GetHandle("Assets/Models/TexturedCube.gltf");
	RenderResources resources(device, project.Assets);

	resources.BeginFrame();
	CHECK(resources.GetTexture(paint, DefaultTexture::White).Width == 4);
	const nvrhi::BindingSetHandle oldBindings = resources.GetMaterial(material).BindingSet;
	const Ref<Model> cube = resources.GetModel(model);
	REQUIRE(cube != nullptr);
	const AssetHandle meshHandle = cube->GetSubAssetHandles(AssetType::Mesh).at(0);
	const GpuMesh* mesh = resources.GetMesh(meshHandle);
	REQUIRE(mesh != nullptr);
	const nvrhi::BufferHandle oldPositions = mesh->Positions; // kept alive: its address cannot be reused

	// A changed file and a reload: the next frame uploads the new texture, and the material binds it.
	writePng(8);
	REQUIRE(project.Assets.ReloadAsset(paint).HasValue());
	REQUIRE(project.Assets.ReloadAsset(model).HasValue());
	CHECK(resources.GetTexture(paint, DefaultTexture::White).Width == 4); // checked this frame already
	resources.BeginFrame();
	CHECK(resources.GetTexture(paint, DefaultTexture::White).Width == 8);
	CHECK(resources.GetMaterial(material).BindingSet != oldBindings);
	const GpuMesh* reloaded = resources.GetMesh(meshHandle);
	REQUIRE(reloaded != nullptr);
	CHECK(reloaded->Positions != oldPositions);
	CHECK(resources.GetModel(model) != cube);
}

TEST_CASE("Renderer: GPU copies that cannot be created are reported once and not retried" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	// The device supports 4 texels per side here: an 8 x 8 texture loads as an asset, its GPU copy cannot be created.
	LimitedGraphicsDevice limited(device, 4);
	RendererProject project;
	RenderResources resources(limited, project.Assets);
	const AssetHandle large(0x7200);
	project.Assets.RegisterBuiltinAsset(large, AssetType::Texture, "Test/Large", [] {
		return Ref<Asset>(CreateRef<Texture2D>(Image(8, 8, ImageFormat::RGBA8), TextureImportSettings{}));
	});
	const auto countErrors = [](const ScopedLogCapture& log) {
		const std::vector<LogEntry> entries = log.GetEntries();
		return std::ranges::count_if(entries, [](const LogEntry& entry) {
			return entry.Level == LogLevel::Error && entry.Message.find("0x0000000000007200") != std::string::npos;
		});
	};

	const ScopedLogCapture log;
	const GpuTexture& white = resources.GetTexture(AssetHandle(), DefaultTexture::White);
	for (int frame = 0; frame < 3; ++frame)
	{
		resources.BeginFrame();
		CHECK(&resources.GetTexture(large, DefaultTexture::White) == &white);
	}
	CHECK(countErrors(log) == 1);
	CHECK(log.Contains(LogLevel::Error, "at most 4 texels per side"));

	// A reload hands out another object: it is tried (and reported) again.
	REQUIRE(project.Assets.ReloadAsset(large).HasValue());
	resources.BeginFrame();
	CHECK(&resources.GetTexture(large, DefaultTexture::White) == &white);
	CHECK(countErrors(log) == 2);

	// A model that fails to load is reported once; after it loaded, a new failure is reported again.
	project.Directory.WriteFile("Assets/Flaky.gltf", "{");
	project.Assets.Scan();
	const AssetHandle flaky = project.Assets.GetHandle("Assets/Flaky.gltf");
	REQUIRE(flaky.IsValid());
	const std::string modelError = std::format("Renderer: asset {}", flaky.ToString());
	const auto countModelErrors = [&] {
		const std::vector<LogEntry> entries = log.GetEntries();
		return std::ranges::count_if(entries, [&](const LogEntry& entry) {
			return entry.Level == LogLevel::Error && entry.Message.find(modelError) != std::string::npos;
		});
	};
	for (int frame = 0; frame < 2; ++frame)
	{
		resources.BeginFrame();
		CHECK(resources.GetModel(flaky) == nullptr);
	}
	CHECK(countModelErrors() == 1);
	project.Directory.WriteFile("Assets/Flaky.gltf", R"({ "asset": { "version": "2.0" } })");
	REQUIRE(project.Assets.ReloadAsset(flaky).HasValue());
	resources.BeginFrame();
	CHECK(resources.GetModel(flaky) != nullptr);
	project.Directory.WriteFile("Assets/Flaky.gltf", "{");
	CHECK_FALSE(project.Assets.ReloadAsset(flaky).HasValue());
	resources.BeginFrame();
	CHECK(resources.GetModel(flaky) == nullptr);
	CHECK(countModelErrors() == 2);
}

TEST_CASE("Renderer: GPU copies hold their assets while used and are released when unused" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RendererProject project;
	project.CopyTexturedCube();
	project.Assets.Scan();
	const AssetHandle model = project.Assets.GetHandle("Assets/Models/TexturedCube.gltf");
	RenderResources resources(device, project.Assets);

	resources.BeginFrame();
	AssetHandle meshHandle;
	{
		const Ref<Model> cube = resources.GetModel(model);
		REQUIRE(cube != nullptr);
		meshHandle = cube->GetSubAssetHandles(AssetType::Mesh).at(0);
	}
	REQUIRE(resources.GetMesh(meshHandle) != nullptr);
	const AssetHandle sphere = MeshFactory::GetHandle(PrimitiveMesh::Sphere);
	REQUIRE(resources.GetMesh(sphere) != nullptr);

	// What the renderer draws is in use: unloading unused assets keeps it (no re-import at the next frame).
	project.Assets.UnloadUnused();
	CHECK(project.Assets.IsLoaded(model));
	CHECK(project.Assets.IsLoaded(meshHandle));
	CHECK(project.Assets.IsLoaded(sphere));

	// The sphere stays in use, the model is no longer asked for: after RenderResources::IdleFramesBeforeRelease
	// frames its GPU copies go, and with them the renderer's hold on the asset.
	for (uint64_t frame = 0; frame <= RenderResources::IdleFramesBeforeRelease; ++frame)
	{
		resources.BeginFrame();
		REQUIRE(resources.GetMesh(sphere) != nullptr);
	}
	project.Assets.UnloadUnused();
	CHECK(project.Assets.IsLoaded(sphere));
	CHECK_FALSE(project.Assets.IsLoaded(model));
	CHECK_FALSE(project.Assets.IsLoaded(meshHandle));
}

TEST_CASE("Renderer: a lit plane has the luminance the BRDF gives" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 32, 32);
	RenderScene scene;
	scene.DrawItems.push_back(MakeDraw(MeshFactory::GetHandle(PrimitiveMesh::Plane), Place(glm::vec3(0.0f), 100.0f)));
	// Looking straight down at the plane, lit straight from above: N = V = L, so the BRDF is
	// diffuse 1 / pi + specular D * Vis * F = 1 / (pi alpha^2) * 1/4 * 0.04 (Schlick at normal incidence).
	CameraComponent camera;
	camera.VerticalFOV = 10.0f;
	const RenderView view = MakeView(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f), camera, 1.0f);
	const float exposure = ExposureFromEV100(view.ExposureEV100);
	const float alpha = 0.5f * 0.5f; // the default material's roughness, squared
	const float brdf = 1.0f / Pi + 0.04f / (4.0f * Pi * alpha * alpha);

	const auto centerLuminance = [&]() {
		const ViewTargets* targets = fixture.Renderer.FindViewTargets(view.ViewId);
		REQUIRE(targets != nullptr);
		return ReadBack(device, targets->SceneColor).GetPixel(16, 16);
	};

	SUBCASE("directional light")
	{
		scene.DirectionalLights.push_back(MakeSun(glm::vec3(0.0f, -1.0f, 0.0f), 0.8f));
		const SceneRenderStats stats = fixture.Render(scene, view);
		CHECK(stats.DrawCalls == 1);
		CHECK(stats.Lights == 1);
		CHECK(stats.Triangles == 2);
		const glm::vec4 luminance = centerLuminance();
		const float expected = brdf * 0.8f * exposure;
		CHECK(luminance.r == doctest::Approx(expected).epsilon(0.01));
		CHECK(luminance.g == doctest::Approx(expected).epsilon(0.01));
		// The output is the tonemapped, sRGB-encoded scene color.
		const glm::vec4 output = ReadBack(device, fixture.Target).GetPixel(16, 16);
		CHECK(output.r == doctest::Approx(LinearToSrgb(TonemapAces(expected))).epsilon(0.01));
		CHECK(output.a == 1.0f);
	}
	SUBCASE("point light: candela over distance squared")
	{
		RenderPointLight bulb;
		bulb.Light.Intensity = 4.0f * Pi * 3.0f; // 3 candela
		bulb.Light.Range = 0.0f;
		bulb.Transform.Previous = bulb.Transform.Current = Place(glm::vec3(0.0f, 1.5f, 0.0f));
		scene.PointLights.push_back(bulb);
		fixture.Render(scene, view);
		const float expected = brdf * (3.0f / (1.5f * 1.5f)) * exposure;
		CHECK(centerLuminance().r == doctest::Approx(expected).epsilon(0.01));
	}
	SUBCASE("spot light: the cone's ramp")
	{
		// 3 candela straight down from 1.5 above, with a narrow cone so that the view also sees where it ends.
		RenderSpotLight spot;
		spot.Light.Intensity = 4.0f * Pi * 3.0f;
		spot.Light.Range = 0.0f;
		spot.Light.InnerConeAngle = 2.0f;
		spot.Light.OuterConeAngle = 4.0f;
		spot.Transform.Previous = spot.Transform.Current =
			Facing(glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
		scene.SpotLights.push_back(spot);
		const SceneRenderStats stats = fixture.Render(scene, view);
		CHECK(stats.Lights == 1);
		const ViewTargets* targets = fixture.Renderer.FindViewTargets(view.ViewId);
		REQUIRE(targets != nullptr);
		const Image luminance = ReadBack(device, targets->SceneColor);
		const float center = luminance.GetPixel(16, 16).r;
		CHECK(center == doctest::Approx(brdf * (3.0f / (1.5f * 1.5f)) * exposure).epsilon(0.01));
		// About 3 degrees off the axis (between the inner and the outer cone): dimmer; 9 degrees: dark.
		const float ramp = luminance.GetPixel(23, 16).r;
		CHECK(ramp > 0.05f * center);
		CHECK(ramp < 0.95f * center);
		CHECK(luminance.GetPixel(0, 0).r == 0.0f);
	}
	SUBCASE("metallic-roughness texture: roughness in G, metallic in B")
	{
		scene.DirectionalLights.push_back(MakeSun(glm::vec3(0.0f, -1.0f, 0.0f), 0.8f));
		Image packed(1, 1, ImageFormat::RGBA8);
		packed.SetPixel(0, 0, glm::vec4(0.0f, 128.0f / 255.0f, 1.0f, 1.0f));
		TextureImportSettings linear;
		linear.Srgb = false;
		linear.GenerateMips = false;
		MaterialData metal;
		metal.BaseColor = glm::vec4(0.9f, 0.6f, 0.3f, 1.0f);
		metal.Metallic = 1.0f;
		metal.Roughness = 1.0f;
		metal.MetallicRoughnessTexture = fixture.AddTexture(packed, linear);
		scene.DrawItems[0].MaterialOverrides = { fixture.AddMaterial(metal) };
		fixture.Render(scene, view);
		// A metal reflects no diffuse light and its base color as F0 (Schlick at normal incidence: F = F0); with
		// N = V = L the specular lobe is D * Vis = 1 / (4 pi alpha^2) for alpha = roughness^2.
		const float roughness = 128.0f / 255.0f;
		const float metalAlpha = roughness * roughness;
		const float specular = 1.0f / (4.0f * Pi * metalAlpha * metalAlpha);
		const glm::vec4 luminance = centerLuminance();
		CHECK(luminance.r == doctest::Approx(0.9f * specular * 0.8f * exposure).epsilon(0.01));
		CHECK(luminance.b == doctest::Approx(0.3f * specular * 0.8f * exposure).epsilon(0.01));
	}
	SUBCASE("uniform ambient light")
	{
		scene.Environment.AmbientLuminance = 0.5f;
		fixture.Render(scene, view);
		// Lambert reflects the radiance times the albedo; the specular part adds Karis' fit, about 0.04 here.
		const float luminance = centerLuminance().r / exposure;
		CHECK(luminance > 0.5f);
		CHECK(luminance < 0.5f * 1.06f);
	}
	SUBCASE("emissive")
	{
		MaterialData glowing;
		glowing.BaseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glowing.EmissiveColor = glm::vec3(0.25f, 0.5f, 1.0f);
		glowing.EmissiveStrength = 2.0f;
		scene.DrawItems[0].MaterialOverrides = { fixture.AddMaterial(glowing) };
		fixture.Render(scene, view);
		const glm::vec4 luminance = centerLuminance();
		CHECK(luminance.r == doctest::Approx(0.5f * exposure).epsilon(0.01));
		CHECK(luminance.b == doctest::Approx(2.0f * exposure).epsilon(0.01));
	}
}

TEST_CASE("Renderer: normal maps shade the same at any scale of the draw" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 32, 32);
	// A normal map that tilts every normal towards the plane's tangent (+X, along +U) to (0.6, 0, 0.8), lit along the
	// tilted normal: N.L is 1 instead of the 0.8 of the flat plane, so the plane is 1.25 times brighter.
	Image tilted(1, 1, ImageFormat::RGBA8);
	tilted.SetPixel(0, 0, glm::vec4(0.8f, 0.5f, 0.9f, 1.0f));
	TextureImportSettings linear;
	linear.Srgb = false;
	linear.GenerateMips = false;
	MaterialData bumpy;
	bumpy.NormalTexture = fixture.AddTexture(tilted, linear);
	const AssetHandle bumpyMaterial = fixture.AddMaterial(bumpy);
	const AssetHandle flatMaterial = fixture.AddMaterial(MaterialData{});
	RenderScene scene;
	scene.DirectionalLights.push_back(MakeSun(-glm::vec3(0.6f, 0.8f, 0.0f), 1.0f));

	// The luminance at the center of the plane, scaled by `scale` and seen from straight above.
	const auto luminanceAt = [&](const glm::vec3& scale, AssetHandle material) {
		scene.DrawItems = { MakeDraw(
			MeshFactory::GetHandle(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), scale), { material }) };
		const float extent = std::min(scale.x, scale.z);
		CameraComponent camera;
		camera.VerticalFOV = 10.0f;
		camera.Near = 0.05f * extent;
		camera.Far = 10.0f * extent;
		const RenderView view = MakeView(glm::vec3(0.0f, 2.0f * extent, 0.0f), glm::vec3(0.0f), camera, 1.0f);
		fixture.Render(scene, view);
		const ViewTargets* targets = fixture.Renderer.FindViewTargets(view.ViewId);
		REQUIRE(targets != nullptr);
		return ReadBack(device, targets->SceneColor).GetPixel(16, 16).r;
	};

	const float flat = luminanceAt(glm::vec3(1.0f), flatMaterial);
	const float reference = luminanceAt(glm::vec3(1.0f), bumpyMaterial);
	CHECK(reference == doctest::Approx(1.25f * flat).epsilon(0.02));
	// Units authored in centimetres (0.01), large props, and non-uniform scale.
	for (const glm::vec3& scale : { glm::vec3(0.01f), glm::vec3(10.0f), glm::vec3(3.0f, 1.0f, 0.5f) })
	{
		CAPTURE(scale.x);
		CAPTURE(scale.z);
		CHECK(luminanceAt(scale, bumpyMaterial) == doctest::Approx(reference).epsilon(0.01));
	}
}

TEST_CASE("Renderer: the PBR sphere grid matches its golden image" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 256, 256);
	const RenderScene scene = MakeSphereGrid(fixture);
	const SceneRenderStats stats = fixture.Render(scene, MakeSphereGridView(fixture.GetAspectRatio()));
	CHECK(stats.DrawItems == 25);
	CHECK(stats.DrawCalls == 25);
	CHECK(stats.CulledDraws == 0);
	CHECK(stats.Lights == 2);
	CheckGoldenTexture(device, fixture.Target, "RendererSphereGrid");
}

TEST_CASE("Renderer: a textured glTF model from a world matches its golden image" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 192, 192);
	fixture.Project.CopyTexturedCube();
	fixture.Project.Assets.Scan();
	const AssetHandle model = fixture.Project.Assets.GetHandle("Assets/Models/TexturedCube.gltf");
	REQUIRE(model.IsValid());

	// The whole path: entities, transform system, extraction, then a view from the scene's camera.
	World world(fixture.Project.Types.Registry);
	REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	auto extraction = CreateScope<RenderExtractionSystem>();
	RenderExtractionSystem& extractor = *extraction;
	REQUIRE(world.GetScheduler().AddSystem(std::move(extraction)).HasValue());
	RenderEnvironment environment;
	environment.AmbientLuminance = 0.3f;
	extractor.SetEnvironment(environment);

	Entity crate = world.CreateEntity("Crate");
	crate.AddComponent<TransformComponent>().Rotation = glm::angleAxis(glm::radians(15.0f), glm::vec3(1, 0, 0));
	crate.AddComponent<MeshRendererComponent>().Mesh = model; // a model draws all its meshes
	Entity sun = world.CreateEntity("Sun");
	sun.AddComponent<TransformComponent>().Rotation =
		glm::quatLookAtRH(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f)), glm::vec3(0.0f, 1.0f, 0.0f));
	sun.AddComponent<DirectionalLightComponent>().Illuminance = 4.0f;
	Entity camera = world.CreateEntity("Camera");
	TransformComponent& cameraTransform = camera.AddComponent<TransformComponent>();
	cameraTransform.Translation = glm::vec3(1.6f, 2.2f, 2.4f);
	cameraTransform.Rotation = glm::quatLookAtRH(
		glm::normalize(glm::vec3(0.0f, 0.4f, 0.0f) - cameraTransform.Translation), glm::vec3(0.0f, 1.0f, 0.0f));
	CameraComponent& lens = camera.AddComponent<CameraComponent>();
	lens.VerticalFOV = 45.0f;
	lens.ClearColor = glm::vec4(0.1f, 0.1f, 0.12f, 1.0f);
	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);

	const RenderScene& scene = extractor.GetRenderScene();
	const RenderCamera* primary = scene.FindPrimaryCamera();
	REQUIRE(primary != nullptr);
	RenderView view = RenderView::FromCamera(*primary, 1.0f, fixture.GetAspectRatio());
	view.ExposureEV100 = 0.0f;
	const SceneRenderStats stats = fixture.Render(scene, view);
	CHECK(stats.DrawItems == 1);
	CHECK(stats.DrawCalls == 1);
	CheckGoldenTexture(device, fixture.Target, "RendererTexturedModel");
}

TEST_CASE("Renderer: alpha-masked and blended materials match their golden image" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 160, 120);
	const AssetHandle quad = MeshFactory::GetHandle(PrimitiveMesh::Quad);
	RenderScene scene;

	// Behind everything: the default checker texture, opaque.
	MaterialData checker;
	checker.BaseColorTexture = GetDefaultTextureHandle(DefaultTexture::Checker);
	checker.UVTiling = glm::vec2(2.0f);
	scene.DrawItems.push_back(
		MakeDraw(quad, Place(glm::vec3(0.0f, 0.0f, -1.0f), 7.0f), { fixture.AddMaterial(checker) }));

	// Left: a disc cut out of a quad by the alpha test, double-sided and seen from behind.
	Image disc(32, 32, ImageFormat::RGBA8);
	for (uint32_t y = 0; y < 32; ++y)
	{
		for (uint32_t x = 0; x < 32; ++x)
		{
			const float distance = glm::length(glm::vec2(x, y) + 0.5f - glm::vec2(16.0f));
			disc.SetPixel(x, y, glm::vec4(1.0f, 1.0f, 1.0f, distance < 13.0f ? 1.0f : 0.0f));
		}
	}
	TextureImportSettings clamp;
	clamp.WrapU = clamp.WrapV = TextureWrap::ClampToEdge;
	MaterialData masked;
	masked.BaseColor = glm::vec4(0.2f, 0.8f, 0.3f, 1.0f);
	masked.BaseColorTexture = fixture.AddTexture(disc, clamp);
	masked.Alpha = AlphaMode::Mask;
	masked.DoubleSided = true;
	scene.DrawItems.push_back(MakeDraw(
		quad,
		Place(glm::vec3(-1.4f, 0.0f, 0.0f), 2.0f, glm::angleAxis(glm::radians(160.0f), glm::vec3(0.0f, 1.0f, 0.0f))),
		{ fixture.AddMaterial(masked) }));

	// Right: three overlapping translucent quads, listed nearest first; drawing them back to front blends correctly.
	const std::array<glm::vec4, 3> colors = { glm::vec4(1.0f, 0.1f, 0.1f, 0.5f), glm::vec4(0.1f, 1.0f, 0.1f, 0.5f),
		                                      glm::vec4(0.1f, 0.1f, 1.0f, 0.5f) };
	for (size_t index = 0; index < colors.size(); ++index)
	{
		MaterialData blended;
		blended.BaseColor = colors[index];
		blended.Alpha = AlphaMode::Blend;
		const float offset = static_cast<float>(index);
		scene.DrawItems.push_back(MakeDraw(
			quad, Place(glm::vec3(0.8f + 0.35f * offset, 0.3f * offset - 0.3f, 0.6f - 0.3f * offset), 1.2f),
			{ fixture.AddMaterial(blended) }));
	}
	scene.DirectionalLights.push_back(MakeSun(glm::vec3(0.2f, -0.3f, -1.0f), 1.5f));
	scene.Environment.AmbientLuminance = 0.25f;

	CameraComponent camera;
	camera.VerticalFOV = 50.0f;
	const SceneRenderStats stats =
		fixture.Render(scene, MakeView(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), camera, fixture.GetAspectRatio()));
	CHECK(stats.DrawCalls == 5);
	CheckGoldenTexture(device, fixture.Target, "RendererAlphaModes");
}

TEST_CASE("Renderer: debug lines and engine shapes match their golden image" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 128, 128);
	RenderScene scene;
	scene.DrawItems.push_back(MakeDraw(
		MeshFactory::GetHandle(PrimitiveMesh::Cube),
		Place(glm::vec3(0.0f), 1.0f, glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)))));
	scene.DirectionalLights.push_back(MakeSun(glm::vec3(-0.5f, -1.0f, -0.3f), 1.5f));
	RenderPointLight bulb;
	bulb.Light.Color = glm::vec3(1.0f, 0.5f, 0.0f);
	bulb.Light.Range = 0.6f;
	bulb.Transform.Previous = bulb.Transform.Current = Place(glm::vec3(-1.2f, 0.9f, 0.0f));
	scene.PointLights.push_back(bulb);
	scene.Environment.AmbientLuminance = 0.3f;

	DebugDrawList lines;
	// A red line through the cube: hidden where the cube covers it. A green one over everything.
	lines.DrawLine(glm::vec3(-2.0f, 0.1f, 0.0f), glm::vec3(2.0f, 0.1f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
	lines.DrawLine(
		glm::vec3(-2.0f, -0.2f, 0.0f), glm::vec3(2.0f, -0.2f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), 0.0f, false);
	lines.DrawSphere(glm::vec3(1.2f, -0.9f, 0.0f), 0.3f, glm::vec4(0.3f, 0.6f, 1.0f, 0.5f));
	scene.DebugLines = lines.GetLines();

	CameraComponent camera;
	camera.VerticalFOV = 50.0f;
	RenderView view = MakeView(glm::vec3(0.5f, 1.5f, 4.0f), glm::vec3(0.0f), camera, fixture.GetAspectRatio());
	view.Shapes = DebugShapes::Bounds | DebugShapes::Lights;
	const SceneRenderStats stats = fixture.Render(scene, view);
	// The scene's two lines and its sphere, the cube's bounds and the light shapes (a sun and a point light with a
	// range).
	CHECK(stats.DebugLines == 2 + 3 * 32 + 12 + (32 + 1) + 2 * 3 * 32);
	CheckGoldenTexture(device, fixture.Target, "RendererDebugLines");

	// Without the scene's lines and shapes only the scene is drawn.
	view.DrawDebugLines = false;
	view.Shapes = DebugShapes::None;
	CHECK(fixture.Render(scene, view).DebugLines == 0);
}

TEST_CASE("Renderer: the tonemap pass sRGB-encodes for UNORM targets only")
{
	CHECK(TonemapPass::NeedsSrgbEncoding(nvrhi::Format::RGBA8_UNORM));
	CHECK(TonemapPass::NeedsSrgbEncoding(nvrhi::Format::BGRA8_UNORM));
	CHECK_FALSE(TonemapPass::NeedsSrgbEncoding(nvrhi::Format::SRGBA8_UNORM));
	CHECK_FALSE(TonemapPass::NeedsSrgbEncoding(nvrhi::Format::SBGRA8_UNORM));
	CHECK_FALSE(TonemapPass::NeedsSrgbEncoding(nvrhi::Format::RGBA16_FLOAT));
	CHECK_FALSE(TonemapPass::NeedsSrgbEncoding(nvrhi::Format::RGBA32_FLOAT));
}

TEST_CASE("Renderer: views of several sizes and formats render in one frame" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 64, 48);
	const RenderScene scene = MakeSphereGrid(fixture);
	const auto makeFramebuffer = [&](const nvrhi::TextureHandle& texture) {
		const nvrhi::FramebufferHandle framebuffer =
			device.GetNvrhiDevice()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture));
		REQUIRE(framebuffer != nullptr);
		return framebuffer;
	};

	// Views of another size: a UNORM target (the shader encodes sRGB itself), an sRGB one and a float one (linear).
	const nvrhi::TextureHandle unorm = CreateTarget(device, 32, 24, nvrhi::Format::RGBA8_UNORM);
	const nvrhi::TextureHandle srgb = CreateTarget(device, 32, 24, nvrhi::Format::SRGBA8_UNORM);
	const nvrhi::TextureHandle linear = CreateTarget(device, 32, 24, nvrhi::Format::RGBA16_FLOAT);
	const nvrhi::FramebufferHandle unormFramebuffer = makeFramebuffer(unorm);
	const nvrhi::FramebufferHandle srgbFramebuffer = makeFramebuffer(srgb);
	const nvrhi::FramebufferHandle linearFramebuffer = makeFramebuffer(linear);
	const auto viewWithId = [](uint64_t id) {
		RenderView view = MakeSphereGridView(4.0f / 3.0f);
		view.ViewId = id;
		return view;
	};
	REQUIRE(fixture.Renderer.Render(scene, viewWithId(1), 1.0f, fixture.Framebuffer).HasValue());
	REQUIRE(fixture.Renderer.Render(scene, viewWithId(2), 1.0f, unormFramebuffer).HasValue());
	REQUIRE(fixture.Renderer.Render(scene, viewWithId(3), 1.0f, srgbFramebuffer).HasValue());
	REQUIRE(fixture.Renderer.Render(scene, viewWithId(4), 1.0f, linearFramebuffer).HasValue());
	const ViewTargets* large = fixture.Renderer.FindViewTargets(1);
	const ViewTargets* unormTargets = fixture.Renderer.FindViewTargets(2);
	const ViewTargets* srgbTargets = fixture.Renderer.FindViewTargets(3);
	const ViewTargets* linearTargets = fixture.Renderer.FindViewTargets(4);
	REQUIRE((large != nullptr && unormTargets != nullptr && srgbTargets != nullptr && linearTargets != nullptr));
	CHECK(large->Size == glm::uvec2(64, 48));
	CHECK(unormTargets->Size == glm::uvec2(32, 24));
	// Views of the same size keep targets of their own: each view's depth and color stay readable after the frame.
	CHECK(unormTargets->SceneColor != srgbTargets->SceneColor);
	CHECK(srgbTargets->Depth != linearTargets->Depth);

	// UNORM and sRGB targets hold the same bytes: one is encoded by the shader, the other by the hardware.
	const Image unormImage = ReadBack(device, unorm);
	const Image srgbImage = ReadBack(device, srgb);
	const Result<ImageCompareResult> same = ImageCompare(unormImage, srgbImage, 1.0f / 255.0f, 0.0);
	REQUIRE(same.HasValue());
	CHECK(same->Passed);
	CHECK(unormImage.GetPixel(16, 12) != unormImage.GetPixel(0, 0)); // a sphere, not the background
	// The float target holds the tonemapped value itself, not encoded.
	const float sceneColor = ReadBack(device, linearTargets->SceneColor).GetPixel(16, 12).r;
	CHECK(ReadBack(device, linear).GetPixel(16, 12).r == doctest::Approx(TonemapAces(sceneColor)).epsilon(0.005));
	CHECK(srgbImage.GetPixel(16, 12).r == doctest::Approx(LinearToSrgb(TonemapAces(sceneColor))).epsilon(0.01));

	// A view rendered at another size replaces its targets.
	REQUIRE(fixture.Renderer.Render(scene, viewWithId(1), 1.0f, unormFramebuffer).HasValue());
	const ViewTargets* resized = fixture.Renderer.FindViewTargets(1);
	REQUIRE(resized != nullptr);
	CHECK(resized->Size == glm::uvec2(32, 24));

	// The targets of views that stopped rendering are released.
	for (uint64_t index = 0; index <= SceneRenderer::TargetIdleRenders; ++index)
	{
		REQUIRE(fixture.Renderer.Render(scene, viewWithId(2), 1.0f, unormFramebuffer).HasValue());
	}
	CHECK(fixture.Renderer.FindViewTargets(1) == nullptr);
	CHECK(fixture.Renderer.FindViewTargets(3) == nullptr);
	CHECK(fixture.Renderer.FindViewTargets(2) != nullptr);
	fixture.Renderer.ReleaseViewTargets();
	CHECK(fixture.Renderer.FindViewTargets(2) == nullptr);

	// Targets the renderer cannot use.
	const RenderView view = viewWithId(2);
	CHECK_FALSE(fixture.Renderer.Render(scene, view, 1.0f, nullptr).HasValue());
	const nvrhi::TextureHandle depth =
		device.GetNvrhiDevice()->createTexture(nvrhi::TextureDesc()
	                                               .setWidth(32)
	                                               .setHeight(24)
	                                               .setFormat(nvrhi::Format::D32)
	                                               .setIsRenderTarget(true)
	                                               .setInitialState(nvrhi::ResourceStates::DepthWrite)
	                                               .setKeepInitialState(true));
	REQUIRE(depth != nullptr);
	const nvrhi::FramebufferHandle withDepth = device.GetNvrhiDevice()->createFramebuffer(
		nvrhi::FramebufferDesc().addColorAttachment(unorm).setDepthAttachment(depth));
	CHECK_FALSE(fixture.Renderer.Render(scene, view, 1.0f, withDepth).HasValue());
}

TEST_CASE("Renderer: mirrored transforms keep their front faces" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 64, 64);
	// A scene symmetric about the YZ plane: mirroring the sphere along X must change nothing. With the wrong winding
	// the mirrored sphere would show its inside.
	RenderScene scene;
	scene.DirectionalLights.push_back(MakeSun(glm::vec3(0.0f, -1.0f, -1.0f), 2.0f));
	scene.Environment.AmbientLuminance = 0.2f;
	CameraComponent camera;
	camera.VerticalFOV = 30.0f;
	const RenderView view = MakeView(glm::vec3(0.0f, 1.0f, 4.0f), glm::vec3(0.0f), camera, 1.0f);
	const AssetHandle sphere = MeshFactory::GetHandle(PrimitiveMesh::Sphere);

	scene.DrawItems = { MakeDraw(sphere, glm::mat4(1.0f)) };
	fixture.Render(scene, view);
	const Image normal = ReadBack(device, fixture.Target);
	scene.DrawItems = { MakeDraw(sphere, glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f))) };
	fixture.Render(scene, view);
	const Image mirrored = ReadBack(device, fixture.Target);

	const Result<ImageCompareResult> compared = ImageCompare(mirrored, normal, 3.0f / 255.0f, 0.01);
	REQUIRE(compared.HasValue());
	CHECK(compared->Passed);
	CHECK(normal.GetPixel(32, 28) != normal.GetPixel(1, 1)); // the sphere is there
}

TEST_CASE("Renderer: draw items outside the view are culled" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 32, 32);
	RenderScene scene;
	const AssetHandle cube = MeshFactory::GetHandle(PrimitiveMesh::Cube);
	scene.DrawItems.push_back(MakeDraw(cube, Place(glm::vec3(0.0f, 0.0f, -5.0f))));
	scene.DrawItems.push_back(MakeDraw(cube, Place(glm::vec3(0.0f, 0.0f, 5.0f))));   // behind the camera
	scene.DrawItems.push_back(MakeDraw(cube, Place(glm::vec3(50.0f, 0.0f, -5.0f)))); // far to the right
	scene.DrawItems.push_back(MakeDraw(UUID(0x1234), Place(glm::vec3(0.0f))));       // no such mesh
	RenderDrawItem missingSubmesh = MakeDraw(cube, Place(glm::vec3(0.0f, 0.0f, -5.0f)));
	missingSubmesh.SubmeshIndex = 3;
	scene.DrawItems.push_back(missingSubmesh);
	CameraComponent camera;
	const ScopedLogCapture log;
	const SceneRenderStats stats =
		fixture.Render(scene, MakeView(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), camera, 1.0f));
	CHECK(stats.DrawItems == 5);
	CHECK(stats.DrawCalls == 1);
	CHECK(stats.CulledDraws == 2);
	CHECK(stats.Triangles == 12);
	CHECK(log.Contains(LogLevel::Warn, "asks for submesh 3"));
}

TEST_CASE(
	"Renderer: a model draws its meshes with their materials, ignoring mesh-only options" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 64, 64);
	fixture.Project.CopyTexturedCube();
	fixture.Project.Assets.Scan();
	const AssetHandle model = fixture.Project.Assets.GetHandle("Assets/Models/TexturedCube.gltf");
	RenderScene scene;
	scene.DrawItems.push_back(MakeDraw(model, glm::mat4(1.0f)));
	scene.DirectionalLights.push_back(MakeSun(glm::vec3(-0.3f, -1.0f, -0.5f), 2.0f));
	CameraComponent camera;
	camera.VerticalFOV = 40.0f;
	const RenderView view = MakeView(glm::vec3(1.5f, 2.0f, 2.5f), glm::vec3(0.0f, 0.5f, 0.0f), camera, 1.0f);
	fixture.Render(scene, view);
	const Image plain = ReadBack(device, fixture.Target);

	// A submesh index and material slots mean nothing across the meshes of a model: warned once, then ignored.
	MaterialData glowing;
	glowing.EmissiveColor = glm::vec3(5.0f);
	scene.DrawItems[0].MaterialOverrides = { fixture.AddMaterial(glowing) };
	scene.DrawItems[0].SubmeshIndex = 3;
	const ScopedLogCapture log;
	CHECK(fixture.Render(scene, view).DrawCalls == 1);
	fixture.Render(scene, view);
	const Result<ImageCompareResult> same = ImageCompare(ReadBack(device, fixture.Target), plain, 0.0f, 0.0);
	REQUIRE(same.HasValue());
	CHECK(same->Passed);
	const std::vector<LogEntry> entries = log.GetEntries();
	CHECK(
		std::ranges::count_if(
			entries,
			[](const LogEntry& entry) {
				return entry.Level == LogLevel::Warn && entry.Message.find("apply to meshes only") != std::string::npos;
			})
		== 1);
}

// A GPU test (GB_GPU_TEST skips it without a device) in the "slow" suite: a test case has one suite.
TEST_CASE("Renderer: the sphere grid renders at 1280x720 (frame time)" * doctest::test_suite("slow"))
{
	GB_GPU_TEST(device);
	RenderFixture fixture(device, 1280, 720);
	const RenderScene scene = MakeSphereGrid(fixture);
	const RenderView view = MakeSphereGridView(fixture.GetAspectRatio());
	fixture.Render(scene, view); // creates the pipelines, targets and GPU copies
	device.WaitIdle();

	constexpr int Frames = 5;
	const auto start = std::chrono::steady_clock::now();
	for (int frame = 0; frame < Frames; ++frame)
	{
		fixture.Render(scene, view);
		device.WaitIdle(); // CPU and GPU time of a frame, as nothing overlaps
	}
	const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
	MESSAGE(std::format(
		"sphere grid at 1280x720 on '{}': {:.1f} ms per frame", device.GetInfo().Name, elapsed.count() / Frames));
	const Image image = ReadBack(device, fixture.Target);
	CHECK(image.GetPixel(640, 360).r > image.GetPixel(0, 0).r + 0.1f); // the middle sphere is lit
}
