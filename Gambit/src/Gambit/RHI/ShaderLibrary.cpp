#include "gbpch.h"

#include "Gambit/RHI/ShaderLibrary.h"

#include "GambitEmbeddedShaders.h"

#include <shared_mutex>

namespace Gambit
{
	namespace
	{
		constexpr uint32_t SpirvMagicNumber = 0x07230203u;
		constexpr size_t SpirvHeaderWords = 5;

		// Shaders registered by other targets. The engine's table is not copied here: it is searched directly.
		struct RegisteredShaders
		{
			std::shared_mutex Mutex;
			std::vector<EmbeddedShader> Shaders; // sorted by name
		};

		RegisteredShaders& GetRegistered()
		{
			static RegisteredShaders registered;
			return registered;
		}

		bool NameLess(const EmbeddedShader& shader, std::string_view name)
		{
			return shader.Name < name;
		}

		std::span<const uint32_t> FindIn(std::span<const EmbeddedShader> shaders, std::string_view name)
		{
			const auto it = std::lower_bound(shaders.begin(), shaders.end(), name, NameLess);
			return it != shaders.end() && it->Name == name ? it->Code : std::span<const uint32_t>();
		}
	}

	std::span<const uint32_t> ShaderLibrary::Find(std::string_view name)
	{
		if (const std::span<const uint32_t> code = FindIn(EmbeddedShaders::GetGambitShaders(), name); !code.empty())
		{
			return code;
		}
		RegisteredShaders& registered = GetRegistered();
		const std::shared_lock lock(registered.Mutex);
		return FindIn(registered.Shaders, name);
	}

	Result<void> ShaderLibrary::Register(std::span<const EmbeddedShader> shaders)
	{
		RegisteredShaders& registered = GetRegistered();
		const std::unique_lock lock(registered.Mutex);

		std::vector<EmbeddedShader> merged = registered.Shaders;
		for (const EmbeddedShader& shader : shaders)
		{
			if (shader.Name.empty())
			{
				return Error("cannot register a shader without a name");
			}
			if (shader.Code.size() < SpirvHeaderWords || shader.Code[0] != SpirvMagicNumber)
			{
				return Error(std::format("cannot register shader '{}': it is not SPIR-V", shader.Name));
			}
			const bool taken = !FindIn(EmbeddedShaders::GetGambitShaders(), shader.Name).empty()
				|| std::ranges::any_of(merged, [&](const EmbeddedShader& other) { return other.Name == shader.Name; });
			if (taken)
			{
				return Error(std::format("cannot register shader '{}': the name is already taken", shader.Name));
			}
			merged.push_back(shader);
		}
		std::ranges::sort(merged, {}, &EmbeddedShader::Name);
		registered.Shaders = std::move(merged);
		return {};
	}

	std::vector<std::string_view> ShaderLibrary::GetNames()
	{
		std::vector<std::string_view> names;
		for (const EmbeddedShader& shader : EmbeddedShaders::GetGambitShaders())
		{
			names.push_back(shader.Name);
		}
		RegisteredShaders& registered = GetRegistered();
		{
			const std::shared_lock lock(registered.Mutex);
			for (const EmbeddedShader& shader : registered.Shaders)
			{
				names.push_back(shader.Name);
			}
		}
		std::ranges::sort(names);
		return names;
	}
}
