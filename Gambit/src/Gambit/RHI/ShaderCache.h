#pragma once

#include "Gambit/Core/Result.h"

#include <nvrhi/nvrhi.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Gambit
{
	// The pipeline stage of a shader from its name's suffix: ".vert" vertex, ".frag" pixel, ".comp" compute, ".geom"
	// geometry; nullopt for anything else.
	[[nodiscard]] std::optional<nvrhi::ShaderType> GetShaderStageFromName(std::string_view name) noexcept;

	// NVRHI shader objects for the embedded shaders of the ShaderLibrary, created on first use and then shared. Each
	// GraphicsDevice owns one (GraphicsDevice::GetShaderCache). Not thread safe.
	class ShaderCache
	{
	public:
		explicit ShaderCache(nvrhi::IDevice& device);

		// The shader called `name` ("FullscreenTriangle.vert") for the stage `stage`, entry point "main". Fails when
		// the library has no such shader, when the name's suffix does not denote `stage`, or when NVRHI rejects the
		// SPIR-V.
		[[nodiscard]] Result<nvrhi::ShaderHandle> Get(std::string_view name, nvrhi::ShaderType stage);
		// The same with the stage taken from the name's suffix.
		[[nodiscard]] Result<nvrhi::ShaderHandle> Get(std::string_view name);

		[[nodiscard]] size_t GetSize() const noexcept { return m_Shaders.size(); }
		// Drops the cache's references; shaders still used elsewhere stay alive until those references go.
		void Clear() noexcept { m_Shaders.clear(); }

	private:
		nvrhi::IDevice& m_Device;
		std::map<std::string, nvrhi::ShaderHandle, std::less<>> m_Shaders;
	};
}
