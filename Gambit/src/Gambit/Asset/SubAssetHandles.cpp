#include "gbpch.h"

#include "Gambit/Asset/SubAssetHandles.h"

namespace Gambit
{
	SubAssetHandles::SubAssetHandles(std::vector<SubAssetEntry> known, std::function<AssetHandle()> generate)
		: m_Entries(std::move(known)), m_Generate(std::move(generate))
	{
		GB_CORE_VERIFY(m_Generate != nullptr, "SubAssetHandles needs a handle generator");
	}

	AssetHandle SubAssetHandles::Acquire(std::string_view key, AssetType type)
	{
		GB_CORE_VERIFY(!key.empty() && type != AssetType::None, "a sub-asset needs a key and a type");
		const auto found = std::ranges::find(m_Entries, key, &SubAssetEntry::Key);
		if (found != m_Entries.end() && found->Type == type)
		{
			return found->Handle;
		}
		// The generator knows the registered assets; the listed handles may not all be registered (a duplicate).
		AssetHandle handle;
		do
		{
			handle = m_Generate();
		} while (!handle.IsValid() || std::ranges::find(m_Entries, handle, &SubAssetEntry::Handle) != m_Entries.end());
		m_Changed = true;
		if (found != m_Entries.end())
		{
			found->Type = type;
			found->Handle = handle;
			return handle;
		}
		m_Entries.push_back({ std::string(key), type, handle });
		return handle;
	}
}
