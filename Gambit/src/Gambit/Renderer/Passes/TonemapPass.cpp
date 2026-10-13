#include "gbpch.h"

#include "Gambit/Renderer/Passes/TonemapPass.h"

#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/PipelineUtils.h"

namespace Gambit
{
	namespace
	{
		struct TonemapConstants
		{
			uint32_t EncodeSrgb = 0;
		};
	}

	TonemapPass::TonemapPass(GraphicsDevice& device)
		: m_Device(device)
	{
		Result<nvrhi::BindingLayoutHandle> layout = CreateBindingLayout(
			device,
			MakeBindingLayoutDesc(
				nvrhi::ShaderType::Pixel, 0,
				{ nvrhi::BindingLayoutItem::Texture_SRV(0),
		          nvrhi::BindingLayoutItem::PushConstants(1, sizeof(TonemapConstants)) }));
		GB_CORE_VERIFY(layout.HasValue(), "{}", layout.GetError().Message);
		m_Layout = *layout;
	}

	bool TonemapPass::NeedsSrgbEncoding(nvrhi::Format format) noexcept
	{
		const nvrhi::FormatInfo& info = nvrhi::getFormatInfo(format);
		return info.kind == nvrhi::FormatKind::Normalized && !info.isSRGB;
	}

	Result<nvrhi::IGraphicsPipeline*> TonemapPass::GetPipeline(const nvrhi::FramebufferInfo& info)
	{
		const auto found = std::ranges::find(
			m_Pipelines, info, &std::pair<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle>::first);
		if (found != m_Pipelines.end())
		{
			return found->second.Get();
		}
		const std::array layouts = { m_Layout };
		Result<nvrhi::GraphicsPipelineHandle> created =
			CreateFullscreenPipeline(m_Device, "Tonemap.frag", info, layouts);
		if (!created)
		{
			return created.GetError();
		}
		m_Pipelines.emplace_back(info, *created);
		return m_Pipelines.back().second.Get();
	}

	Result<void>
	TonemapPass::Execute(nvrhi::ICommandList& commandList, nvrhi::ITexture* sceneColor, nvrhi::IFramebuffer* output)
	{
		const nvrhi::FramebufferInfoEx& info = output->getFramebufferInfo();
		if (info.colorFormats.size() != 1)
		{
			return Error("the tonemap pass writes to a framebuffer with one color attachment");
		}
		Result<nvrhi::IGraphicsPipeline*> pipeline = GetPipeline(info);
		if (!pipeline)
		{
			return pipeline.GetError();
		}
		if (m_BindingSet == nullptr || m_BoundSceneColor != sceneColor)
		{
			Result<nvrhi::BindingSetHandle> set = CreateBindingSet(
				m_Device, m_Layout,
				{ nvrhi::BindingSetItem::Texture_SRV(0, sceneColor),
			      nvrhi::BindingSetItem::PushConstants(1, sizeof(TonemapConstants)) });
			if (!set)
			{
				return set.GetError();
			}
			m_BindingSet = *set;
			m_BoundSceneColor = sceneColor;
		}
		const TonemapConstants constants{ NeedsSrgbEncoding(info.colorFormats[0]) ? 1u : 0u };
		DrawFullscreenTriangle(
			commandList, MakeGraphicsState(*pipeline, output).addBindingSet(m_BindingSet),
			std::as_bytes(std::span(&constants, 1)));
		return {};
	}
}
