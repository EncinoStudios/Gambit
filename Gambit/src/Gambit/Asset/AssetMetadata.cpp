#include "gbpch.h"

#include "Gambit/Asset/AssetMetadata.h"

#include "Gambit/Serialization/FieldSerializer.h"

#include <set>

namespace Gambit
{
	namespace
	{
		Result<std::vector<SubAssetEntry>> ParseSubAssets(const Json& array, std::string_view path, LoadReport& report)
		{
			if (!array.is_array())
			{
				return JsonTypeError(path, "an array", array);
			}
			std::vector<SubAssetEntry> entries;
			std::set<std::string, std::less<>> keys;
			std::set<AssetHandle> handles;
			for (size_t index = 0; index < array.size(); ++index)
			{
				const Json& item = array[index];
				const std::string itemPath = JsonPathAppend(path, index);
				if (!item.is_object())
				{
					return JsonTypeError(itemPath, "an object", item);
				}
				SubAssetEntry entry;
				bool hasKey = false;
				bool hasType = false;
				bool hasHandle = false;
				for (const auto& [name, value] : item.items())
				{
					const std::string valuePath = JsonPathAppend(itemPath, name);
					if (name == "Key")
					{
						if (!value.is_string() || value.get_ref<const std::string&>().empty())
						{
							return MakePathError(valuePath, "expected a non-empty string");
						}
						entry.Key = value.get<std::string>();
						hasKey = true;
					}
					else if (name == "Type")
					{
						const std::optional<AssetType> type =
							value.is_string() ? ParseAssetType(value.get_ref<const std::string&>()) : std::nullopt;
						if (!type.has_value())
						{
							return MakePathError(valuePath, "expected the name of an asset type like \"Mesh\"");
						}
						entry.Type = *type;
						hasType = true;
					}
					else if (name == "Handle")
					{
						const std::optional<UUID> handle =
							value.is_string() ? UUID::Parse(value.get_ref<const std::string&>()) : std::nullopt;
						if (!handle.has_value() || !handle->IsValid())
						{
							return MakePathError(valuePath, "expected a non-null handle like \"0x00000000DEADBEEF\"");
						}
						entry.Handle = *handle;
						hasHandle = true;
					}
					else
					{
						report.Warn(valuePath, "unknown member, ignored");
					}
				}
				if (!hasKey || !hasType || !hasHandle)
				{
					return MakePathError(itemPath, "a sub-asset needs \"Key\", \"Type\" and \"Handle\"");
				}
				if (!keys.insert(entry.Key).second)
				{
					return MakePathError(itemPath, std::format("the key '{}' is listed twice", entry.Key));
				}
				if (!handles.insert(entry.Handle).second)
				{
					return MakePathError(
						itemPath, std::format("the handle {} is listed twice", entry.Handle.ToString()));
				}
				entries.push_back(std::move(entry));
			}
			return entries;
		}
	}

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
			else if (key == "SubAssets")
			{
				Result<std::vector<SubAssetEntry>> subAssets = ParseSubAssets(member, path, report);
				if (!subAssets)
				{
					return AddSource(source, subAssets.GetError());
				}
				metadata.SubAssets = std::move(*subAssets);
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
		if (!SubAssets.empty())
		{
			Json subAssets = Json::array();
			for (const SubAssetEntry& entry : SubAssets)
			{
				Json item = Json::object();
				item["Key"] = entry.Key;
				item["Type"] = std::string(ToString(entry.Type));
				item["Handle"] = entry.Handle.ToString();
				subAssets.push_back(std::move(item));
			}
			document["SubAssets"] = std::move(subAssets);
		}
		return DumpJson(document);
	}
}
