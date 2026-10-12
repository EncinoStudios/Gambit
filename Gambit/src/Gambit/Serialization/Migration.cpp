#include "gbpch.h"

#include "Gambit/Serialization/Migration.h"

namespace Gambit
{
	void MigrationTable::Add(uint32_t from, Step step)
	{
		GB_CORE_VERIFY(from >= 1, "migration steps start at version 1");
		GB_CORE_VERIFY(step != nullptr, "migration step {} is empty", from);
		GB_CORE_VERIFY(m_Steps.emplace(from, std::move(step)).second, "migration step {} is registered twice", from);
	}

	Result<void> MigrationTable::Upgrade(Json& document, uint32_t currentVersion, std::string_view format) const
	{
		if (!document.is_object())
		{
			return Error::Format("a {} file must be a JSON object", format);
		}
		const auto found = document.find("Version");
		if (found == document.end())
		{
			return Error::Format("the {} file has no \"Version\"", format);
		}
		if (!found->is_number_integer() || found->get<int64_t>() < 1 || found->get<int64_t>() > UINT32_MAX)
		{
			return Error::Format("the {} file's \"Version\" must be a positive integer", format);
		}

		auto version = static_cast<uint32_t>(found->get<int64_t>());
		if (version > currentVersion)
		{
			return Error::Format(
				"the {} file has version {} but this version of Gambit reads up to version {}: it was written by a "
				"newer Gambit",
				format, version, currentVersion);
		}
		while (version < currentVersion)
		{
			const auto step = m_Steps.find(version);
			if (step == m_Steps.end())
			{
				return Error::Format(
					"the {} file has version {} and there is no migration to version {}", format, version, version + 1);
			}
			// Steps edit untrusted documents with the JSON library, whose accessors throw on unexpected shapes; the
			// exception stops here like any other failed step.
			Result<void> upgraded;
			try
			{
				upgraded = step->second(document);
			}
			catch (const std::exception& exception)
			{
				upgraded = Error(exception.what());
			}
			if (!upgraded)
			{
				return Error::Format(
					"migrating the {} file from version {} to {} failed: {}", format, version, version + 1,
					upgraded.GetError().Message);
			}
			++version;
			document["Version"] = version;
		}
		return {};
	}

	const MigrationTable& GetNoMigrations()
	{
		static const MigrationTable s_Table;
		return s_Table;
	}
}
