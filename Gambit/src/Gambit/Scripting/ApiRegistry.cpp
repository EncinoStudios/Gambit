#include "gbpch.h"

#include "Gambit/Scripting/ApiRegistry.h"

namespace Gambit
{
	std::string_view ToString(ApiEntryKind kind) noexcept
	{
		switch (kind)
		{
			case ApiEntryKind::Function:
				return "Function";
			case ApiEntryKind::Method:
				return "Method";
			case ApiEntryKind::Property:
				return "Property";
			case ApiEntryKind::Constant:
				return "Constant";
			case ApiEntryKind::Type:
				return "Type";
			case ApiEntryKind::Table:
				return "Table";
		}
		return "Unknown";
	}

	ApiRegistry& ApiRegistry::Get()
	{
		static ApiRegistry& s_Registry = []() -> ApiRegistry& {
			static ApiRegistry registry;
			RegisterEngineScriptApi(registry);
			return registry;
		}();
		return s_Registry;
	}

	void ApiRegistry::RegisterBinder(std::string_view name, ScriptBinderFunction bind)
	{
		GB_CORE_VERIFY(!name.empty() && bind != nullptr, "a script binder needs a name and a function");
		GB_CORE_VERIFY(
			std::ranges::none_of(m_Binders, [&](const ScriptBinder& binder) { return binder.Name == name; }),
			"the script binder '{}' is registered twice", name);
		m_Binders.push_back(ScriptBinder{ std::string(name), std::move(bind) });
	}

	void ApiRegistry::Document(ApiEntry entry)
	{
		GB_CORE_VERIFY(!entry.Name.empty(), "an API entry needs a name");
		GB_CORE_VERIFY(
			!entry.Signature.empty() && !entry.Description.empty(),
			"the API entry '{}' needs a signature and a description", entry.Name);
		GB_CORE_VERIFY(!m_Entries.contains(entry.Name), "the API entry '{}' is documented twice", entry.Name);
		std::string name = entry.Name;
		m_Entries.emplace(std::move(name), std::move(entry));
	}

	void ApiRegistry::Document(
		std::string_view name, ApiEntryKind kind, std::string_view signature, std::string_view description,
		std::string_view example)
	{
		Document(ApiEntry{
			.Name = std::string(name),
			.Kind = kind,
			.Signature = std::string(signature),
			.Description = std::string(description),
			.Example = std::string(example),
			.Module = m_Binders.empty() ? std::string() : m_Binders.back().Name,
		});
	}

	const ApiEntry* ApiRegistry::Find(std::string_view name) const
	{
		const auto found = m_Entries.find(name);
		return found == m_Entries.end() ? nullptr : &found->second;
	}

	std::vector<const ApiEntry*> ApiRegistry::GetEntries() const
	{
		std::vector<const ApiEntry*> entries;
		entries.reserve(m_Entries.size());
		for (const auto& [name, entry] : m_Entries)
		{
			entries.push_back(&entry);
		}
		return entries;
	}
}
