#pragma once

#include "Gambit/Asset/AssetType.h"

#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// A loaded asset (D-014): data produced by an importer from a file of the project. The AssetManager caches assets
	// and sets the handle and path when it loads one; assets created in memory have a null handle and an empty path.
	// Derive one class per AssetType with `static constexpr AssetType StaticType` so that AssetManager::GetAsset<T>
	// can check types.
	class Asset
	{
	public:
		virtual ~Asset() = default;
		Asset(const Asset&) = delete;
		Asset& operator=(const Asset&) = delete;

		[[nodiscard]] virtual AssetType GetType() const noexcept = 0;

		[[nodiscard]] AssetHandle GetHandle() const noexcept { return m_Handle; }
		// Project-relative path with forward slashes ("Assets/Meshes/Crate.glb").
		[[nodiscard]] const std::string& GetPath() const noexcept { return m_Path; }

		// The assets this one refers to by handle (a scene's prefabs and textures, a table's asset columns): sorted,
		// distinct, without the null handle. They are not loaded together with the asset.
		[[nodiscard]] virtual std::vector<AssetHandle> GetDependencies() const { return {}; }

	protected:
		Asset() = default;

	private:
		friend class AssetManager;

		AssetHandle m_Handle;
		std::string m_Path;
	};
}
