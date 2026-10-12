#include "gbpch.h"

#include "Gambit/RHI/ShaderCache.h"

#include "Gambit/RHI/ShaderLibrary.h"

#include <nvrhi/utils.h>

namespace Gambit
{
	std::optional<nvrhi::ShaderType> GetShaderStageFromName(std::string_view name) noexcept
	{
		const size_t dot = name.rfind('.');
		if (dot == std::string_view::npos || dot == 0)
		{
			return std::nullopt;
		}
		const std::string_view suffix = name.substr(dot + 1);
		if (suffix == "vert")
		{
			return nvrhi::ShaderType::Vertex;
		}
		if (suffix == "frag")
		{
			return nvrhi::ShaderType::Pixel;
		}
		if (suffix == "comp")
		{
			return nvrhi::ShaderType::Compute;
		}
		if (suffix == "geom")
		{
			return nvrhi::ShaderType::Geometry;
		}
		return std::nullopt;
	}

	ShaderCache::ShaderCache(nvrhi::IDevice& device)
		: m_Device(device)
	{}

	Result<nvrhi::ShaderHandle> ShaderCache::Get(std::string_view name, nvrhi::ShaderType stage)
	{
		const std::optional<nvrhi::ShaderType> nameStage = GetShaderStageFromName(name);
		if (!nameStage || *nameStage != stage)
		{
			return Error(std::format(
				"shader '{}' is not a {} shader (names end in .vert, .frag, .comp or .geom)", name,
				nvrhi::utils::ShaderStageToString(stage)));
		}
		if (const auto it = m_Shaders.find(name); it != m_Shaders.end())
		{
			return it->second;
		}

		const std::span<const uint32_t> code = ShaderLibrary::Find(name);
		if (code.empty())
		{
			return Error(std::format("there is no shader '{}' in the shader library", name));
		}
		const std::string debugName(name);
		const nvrhi::ShaderDesc desc =
			nvrhi::ShaderDesc().setShaderType(stage).setDebugName(debugName).setEntryName("main");
		nvrhi::ShaderHandle shader = m_Device.createShader(desc, code.data(), code.size_bytes());
		if (shader == nullptr)
		{
			return Error(std::format("NVRHI could not create shader '{}' (see the log)", name));
		}
		m_Shaders.emplace(debugName, shader);
		return shader;
	}

	Result<nvrhi::ShaderHandle> ShaderCache::Get(std::string_view name)
	{
		const std::optional<nvrhi::ShaderType> stage = GetShaderStageFromName(name);
		if (!stage)
		{
			return Error(
				std::format("cannot tell the stage of shader '{}' (names end in .vert, .frag, .comp or .geom)", name));
		}
		return Get(name, *stage);
	}
}
