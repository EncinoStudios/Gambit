#pragma once

#include "Gambit/Core/Result.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// The JSON document type of every Gambit file and of the automation API (D-025). Objects keep the order in which
	// keys were inserted, so a document is written exactly in the order the code builds it ("stable key order").
	using Json = nlohmann::ordered_json;

	// Documents nested deeper than this are rejected (a guard against hostile input; real files stay far below it).
	inline constexpr size_t MaxJsonDepth = 64;

	// Parses strict JSON (no comments, no trailing commas; a leading UTF-8 byte order mark is dropped). Never throws:
	// errors say where the text is malformed ("parse error at line 3, column 5: ..."). `source` names the text in
	// errors (usually the file path).
	[[nodiscard]] Result<Json> ParseJson(std::string_view text, std::string_view source = {});

	// The canonical text of a document: 2-space indent, keys in insertion order, non-ASCII characters as UTF-8 and a
	// trailing newline. Never throws: text that is not valid UTF-8 is an error.
	[[nodiscard]] Result<std::string> DumpJson(const Json& document);

	// JSON pointers (RFC 6901) of members and elements, used to say where in a document a problem is:
	// "/Entities/3/Components/Transform". "~" and "/" in keys are escaped.
	[[nodiscard]] std::string JsonPathAppend(std::string_view path, std::string_view key);
	[[nodiscard]] std::string JsonPathAppend(std::string_view path, size_t index);

	// "path: message", or just the message for the document root.
	[[nodiscard]] Error MakePathError(std::string_view path, std::string_view message);

	// The error with "source: " (a file path, say) in front of its message; the code is kept. An empty source changes
	// nothing.
	[[nodiscard]] Error AddSource(std::string_view source, const Error& error);

	// A problem found while reading a document that does not stop the load: an unknown field, an ignored component.
	struct Diagnostic
	{
		std::string Path; // JSON pointer of the offending element
		std::string Message;
	};

	// Collects the warnings of a load so that callers can show them (the CLI and MCP return them in their results;
	// file loaders also log them). Keeps the first MaxStored and counts the rest.
	class LoadReport
	{
	public:
		static constexpr size_t MaxStored = 100;

		void Warn(std::string_view path, std::string message);

		[[nodiscard]] std::span<const Diagnostic> GetWarnings() const noexcept { return m_Warnings; }
		// Warnings beyond MaxStored, which were counted but not kept.
		[[nodiscard]] size_t GetSuppressedCount() const noexcept { return m_Suppressed; }
		[[nodiscard]] bool IsEmpty() const noexcept { return m_Warnings.empty(); }

		// Logs every warning as "<source>: <path>: <message>".
		void Log(std::string_view source) const;

	private:
		std::vector<Diagnostic> m_Warnings;
		size_t m_Suppressed = 0;
	};
}
