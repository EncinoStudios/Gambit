#include "gbpch.h"

#include "Gambit/Asset/AssetType.h"

namespace Gambit
{
	namespace
	{
		struct TypeName
		{
			AssetType Type;
			std::string_view Name;
		};

		constexpr std::array<TypeName, 15> TypeNames = { {
			{ AssetType::None, "None" },
			{ AssetType::Scene, "Scene" },
			{ AssetType::Prefab, "Prefab" },
			{ AssetType::Texture, "Texture" },
			{ AssetType::Mesh, "Mesh" },
			{ AssetType::Material, "Material" },
			{ AssetType::Environment, "Environment" },
			{ AssetType::Script, "Script" },
			{ AssetType::Audio, "Audio" },
			{ AssetType::Font, "Font" },
			{ AssetType::VectorGraphic, "VectorGraphic" },
			{ AssetType::DataTable, "DataTable" },
			{ AssetType::AnimationClip, "AnimationClip" },
			{ AssetType::Skeleton, "Skeleton" },
			{ AssetType::InputActions, "InputActions" },
		} };

		struct Extension
		{
			std::string_view Text; // lower case, with the dot
			AssetType Type;
		};

		constexpr std::array<Extension, 20> Extensions = { {
			{ ".gscene", AssetType::Scene },      { ".gprefab", AssetType::Prefab },  { ".png", AssetType::Texture },
			{ ".jpg", AssetType::Texture },       { ".jpeg", AssetType::Texture },    { ".tga", AssetType::Texture },
			{ ".bmp", AssetType::Texture },       { ".hdr", AssetType::Environment }, { ".glb", AssetType::Mesh },
			{ ".gltf", AssetType::Mesh },         { ".gmat", AssetType::Material },   { ".lua", AssetType::Script },
			{ ".wav", AssetType::Audio },         { ".ogg", AssetType::Audio },       { ".mp3", AssetType::Audio },
			{ ".flac", AssetType::Audio },        { ".ttf", AssetType::Font },        { ".otf", AssetType::Font },
			{ ".svg", AssetType::VectorGraphic }, { ".csv", AssetType::DataTable },
		} };

		constexpr std::string_view InputActionsFileName = "InputActions.json";

		bool EqualsIgnoringCase(std::string_view text, std::string_view lowerCase) noexcept
		{
			return text.size() == lowerCase.size()
				&& std::equal(text.begin(), text.end(), lowerCase.begin(), [](char a, char b) {
					   const char lowered = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
					   return lowered == b;
				   });
		}
	}

	std::string_view ToString(AssetType type) noexcept
	{
		for (const TypeName& entry : TypeNames)
		{
			if (entry.Type == type)
			{
				return entry.Name;
			}
		}
		return "Unknown";
	}

	std::optional<AssetType> ParseAssetType(std::string_view name) noexcept
	{
		for (const TypeName& entry : TypeNames)
		{
			if (entry.Type != AssetType::None && entry.Name == name)
			{
				return entry.Type;
			}
		}
		return std::nullopt;
	}

	AssetType DetectAssetType(std::string_view path) noexcept
	{
		const size_t slash = path.find_last_of("/\\");
		const std::string_view fileName = slash == std::string_view::npos ? path : path.substr(slash + 1);
		if (fileName == InputActionsFileName)
		{
			return AssetType::InputActions;
		}
		const size_t dot = fileName.find_last_of('.');
		if (dot == std::string_view::npos || dot == 0)
		{
			return AssetType::None;
		}
		const std::string_view extension = fileName.substr(dot);
		for (const Extension& entry : Extensions)
		{
			if (EqualsIgnoringCase(extension, entry.Text))
			{
				return entry.Type;
			}
		}
		return AssetType::None;
	}
}
