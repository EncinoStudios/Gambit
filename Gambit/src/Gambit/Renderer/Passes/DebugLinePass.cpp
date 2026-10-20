#include "gbpch.h"

#include "Gambit/Renderer/Passes/DebugLinePass.h"

#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/PipelineUtils.h"
#include "Gambit/RHI/ShaderCache.h"
#include "Gambit/Renderer/GpuTypes.h"
#include "Gambit/Renderer/Passes/TonemapPass.h"

#include <bit>

namespace Gambit
{
	namespace
	{
		struct LineConstants
		{
			glm::mat4 ViewProjection{ 1.0f };
			uint32_t EncodeSrgb = 0;
			uint32_t Padding[3] = {};
		};
	}

	DebugLinePass::DebugLinePass(GraphicsDevice& device)
		: m_Device(device)
	{
		Result<nvrhi::BindingLayoutHandle> layout = CreateBindingLayout(
			device,
			MakeBindingLayoutDesc(
				nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel, 0,
				{ nvrhi::BindingLayoutItem::PushConstants(0, sizeof(LineConstants)) }));
		GB_CORE_VERIFY(layout.HasValue(), "{}", layout.GetError().Message);
		m_Layout = *layout;
		Result<nvrhi::BindingSetHandle> set =
			CreateBindingSet(device, m_Layout, { nvrhi::BindingSetItem::PushConstants(0, sizeof(LineConstants)) });
		GB_CORE_VERIFY(set.HasValue(), "{}", set.GetError().Message);
		m_BindingSet = *set;

		const std::array attributes = {
			nvrhi::VertexAttributeDesc()
				.setName("POSITION")
				.setFormat(nvrhi::Format::RGB32_FLOAT)
				.setOffset(offsetof(Gpu::LineVertex, Position))
				.setElementStride(sizeof(Gpu::LineVertex)),
			nvrhi::VertexAttributeDesc()
				.setName("COLOR")
				.setFormat(nvrhi::Format::RGBA32_FLOAT)
				.setOffset(offsetof(Gpu::LineVertex, Color))
				.setElementStride(sizeof(Gpu::LineVertex)),
		};
		Result<nvrhi::ShaderHandle> vertexShader = device.GetShaderCache().Get("DebugLine.vert");
		GB_CORE_VERIFY(vertexShader.HasValue(), "{}", vertexShader.GetError().Message);
		m_InputLayout = device.GetNvrhiDevice()->createInputLayout(
			attributes.data(), static_cast<uint32_t>(attributes.size()), *vertexShader);
		GB_CORE_VERIFY(m_InputLayout != nullptr, "NVRHI could not create the debug line input layout");
	}

	Result<nvrhi::IGraphicsPipeline*> DebugLinePass::GetPipeline(const nvrhi::FramebufferInfo& info, bool depthTested)
	{
		for (const auto& [pipelineInfo, pipelineDepthTested, pipeline] : m_Pipelines)
		{
			if (pipelineInfo == info && pipelineDepthTested == depthTested)
			{
				return pipeline.Get();
			}
		}
		ShaderCache& shaders = m_Device.GetShaderCache();
		Result<nvrhi::ShaderHandle> vertexShader = shaders.Get("DebugLine.vert");
		Result<nvrhi::ShaderHandle> pixelShader = shaders.Get("DebugLine.frag");
		if (!vertexShader || !pixelShader)
		{
			return (!vertexShader ? vertexShader : pixelShader).GetError();
		}
		nvrhi::GraphicsPipelineDesc desc;
		desc.setPrimType(nvrhi::PrimitiveType::LineList)
			.setInputLayout(m_InputLayout)
			.setVertexShader(*vertexShader)
			.setPixelShader(*pixelShader)
			.addBindingLayout(m_Layout);
		nvrhi::RenderState& state = desc.renderState;
		state.rasterState.setCullNone().enableDepthClip();
		state.depthStencilState.disableDepthWrite().disableStencil();
		if (depthTested)
		{
			state.depthStencilState.enableDepthTest().setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
		}
		else
		{
			state.depthStencilState.disableDepthTest();
		}
		state.blendState.targets[0]
			.enableBlend()
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
			.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One)
			.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		nvrhi::GraphicsPipelineHandle pipeline = m_Device.GetNvrhiDevice()->createGraphicsPipeline(desc, info);
		if (!pipeline)
		{
			return Error("NVRHI could not create the debug line pipeline (see the log)");
		}
		m_Pipelines.emplace_back(info, depthTested, pipeline);
		return pipeline.Get();
	}

	Result<void> DebugLinePass::Execute(
		nvrhi::ICommandList& commandList, std::span<const DebugLine> lines, const glm::mat4& viewProjection,
		nvrhi::IFramebuffer* output)
	{
		if (lines.empty())
		{
			return {};
		}
		const nvrhi::FramebufferInfoEx& info = output->getFramebufferInfo();
		if (info.colorFormats.size() != 1 || info.depthFormat == nvrhi::Format::UNKNOWN)
		{
			return Error("the debug line pass draws into a framebuffer with one color attachment and depth");
		}

		// Depth-tested lines first, then the overlay: two ranges of one vertex buffer.
		std::vector<Gpu::LineVertex> vertices;
		vertices.reserve(lines.size() * 2);
		for (const bool depthTested : { true, false })
		{
			for (const DebugLine& line : lines)
			{
				if (line.DepthTested == depthTested)
				{
					vertices.push_back({ line.Start, line.Color });
					vertices.push_back({ line.End, line.Color });
				}
			}
		}
		const auto testedVertices = static_cast<uint32_t>(
			2 * std::ranges::count_if(lines, [](const DebugLine& line) { return line.DepthTested; }));

		if (vertices.size() > m_VertexCapacity)
		{
			m_VertexCapacity = std::bit_ceil(vertices.size());
			m_Vertices =
				m_Device.GetNvrhiDevice()->createBuffer(nvrhi::BufferDesc()
			                                                .setByteSize(m_VertexCapacity * sizeof(Gpu::LineVertex))
			                                                .setIsVertexBuffer(true)
			                                                .setInitialState(nvrhi::ResourceStates::VertexBuffer)
			                                                .setKeepInitialState(true)
			                                                .setDebugName("Debug lines"));
			if (!m_Vertices)
			{
				m_VertexCapacity = 0;
				return Error("NVRHI could not create the debug line vertex buffer (see the log)");
			}
		}
		commandList.writeBuffer(m_Vertices, vertices.data(), vertices.size() * sizeof(Gpu::LineVertex));

		LineConstants constants;
		constants.ViewProjection = viewProjection;
		constants.EncodeSrgb = TonemapPass::NeedsSrgbEncoding(info.colorFormats[0]) ? 1u : 0u;
		const std::array<std::pair<uint32_t, uint32_t>, 2> ranges = {
			std::make_pair(0u, testedVertices),
			std::make_pair(testedVertices, static_cast<uint32_t>(vertices.size()) - testedVertices),
		};
		for (size_t index = 0; index < ranges.size(); ++index)
		{
			const auto [first, count] = ranges[index];
			if (count == 0)
			{
				continue;
			}
			Result<nvrhi::IGraphicsPipeline*> pipeline = GetPipeline(info, index == 0);
			if (!pipeline)
			{
				return pipeline.GetError();
			}
			commandList.setGraphicsState(
				MakeGraphicsState(*pipeline, output)
					.addBindingSet(m_BindingSet)
					.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(m_Vertices).setSlot(0).setOffset(0)));
			commandList.setPushConstants(&constants, sizeof(constants));
			commandList.draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
		}
		return {};
	}
}
