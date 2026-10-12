#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Serialization/Json.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string_view>

namespace Gambit
{
	// Upgrades the documents of one file format that older versions of the engine wrote (D-025: every format carries
	// "Version" with migration code). A step rewrites a version N document into a version N + 1 one; a document is
	// upgraded step by step to the version the engine writes today. Every format with a "Version" has such a table:
	// scenes, prefabs, projects, asset metadata. Today every format is at version 1, so the tables are empty.
	class MigrationTable
	{
	public:
		// Rewrites `document` in place. The table sets "Version" itself after each step. An exception thrown by a step
		// (the JSON library's accessors throw on unexpected shapes) fails the upgrade like an error.
		using Step = std::function<Result<void>(Json& document)>;

		// Registers the step that upgrades documents of version `from`. A version can have one step (verified).
		void Add(uint32_t from, Step step);

		// Reads "Version" (a positive integer) of `document`, an object, and applies the steps from it up to
		// `currentVersion`, updating "Version" along the way. Fails, leaving the document as far as it got, when the
		// document is no object, has no valid "Version", was written by a newer engine, lacks a step or a step fails.
		// `format` names the file kind in messages ("scene").
		[[nodiscard]] Result<void> Upgrade(Json& document, uint32_t currentVersion, std::string_view format) const;

	private:
		std::map<uint32_t, Step> m_Steps;
	};

	// A table without steps, for formats that have only ever had one version.
	[[nodiscard]] const MigrationTable& GetNoMigrations();
}
