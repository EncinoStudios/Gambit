#include "gbpch.h"

#include "Gambit/Asset/AssetMetadata.h"

#include "Gambit/Serialization/FieldSerializer.h"

namespace Gambit
{
	std::string AssetMetadata::GetMetaPath(std::string_view assetPath)
	{
		return std::string(assetPath) + std::string(FileExtension);
	}

	bool AssetMetadata::IsMetaPath(std::string_view path) noexcept
	{
		return path.size() > FileExtension.size() && path.ends_with(FileExtension);
	}

	const MigrationTable& AssetMetadata::GetMigrations()
	{
		return GetNoMigrations();
	}

	Result<AssetMetadata> AssetMetadata::Parse(std::string_view text, std::string_view source, LoadReport& report)
	{
		Result<Json> document = ParseJson(text, source);
		if (!document)
		{
			return document.GetError();
		}
		if (Result<void> upgraded = GetMigrations().Upgrade(*document, FormatVersion, "asset metadata"); !upgraded)
		{
			return AddSource(source, upgraded.GetError());
		}

		AssetMetadata metadata;
		for (const auto& [key, member] : document->items())
		{
			const std::string path = JsonPathAppend({}, key);
			if (key == "Handle")
			{
				const std::optional<UUID> handle =
					member.is_string() ? UUID::Parse(member.get_ref<const std::string&>()) : std::nullopt;
				if (!handle.has_value())
				{
					return AddSource(
						source, MakePathError(path, "expected a handle string like \"0x00000000DEADBEEF\""));
				}
				metadata.Handle = *handle;
			}
			else if (key == "Type")
			{
				const std::optional<AssetType> type =
					member.is_string() ? ParseAssetType(member.get_ref<const std::string&>()) : std::nullopt;
				if (!type.has_value())
				{
					return AddSource(
						source, MakePathError(path, "expected the name of an asset type like \"Texture\""));
				}
				metadata.Type = *type;
			}
			else if (key == "ImportSettings")
			{
				if (!member.is_object())
				{
					return AddSource(source, JsonTypeError(path, "an object", member));
				}
				metadata.ImportSettings = member;
			}
			else if (key != "Version")
			{
				report.Warn(path, "unknown member, ignored");
			}
		}
		return metadata;
	}

	Result<std::string> AssetMetadata::Serialize() const
	{
		Json document = Json::object();
		document["Version"] = FormatVersion;
		document["Handle"] = Handle.ToString();
		document["Type"] = std::string(ToString(Type));
		document["ImportSettings"] = ImportSettings;
		return DumpJson(document);
	}
}
