#include "gbpch.h"

#include "Gambit/Serialization/Json.h"

namespace Gambit
{
	namespace
	{
		// Whether the brackets of `text` nest deeper than MaxJsonDepth; strings are skipped. A cheap scan that keeps
		// hostile input away from the recursive parts of the JSON library.
		bool ExceedsDepth(std::string_view text) noexcept
		{
			size_t depth = 0;
			bool inString = false;
			for (size_t index = 0; index < text.size(); ++index)
			{
				const char character = text[index];
				if (inString)
				{
					if (character == '\\')
					{
						++index; // the escaped character cannot end the string
					}
					else if (character == '"')
					{
						inString = false;
					}
				}
				else if (character == '"')
				{
					inString = true;
				}
				else if (character == '{' || character == '[')
				{
					if (++depth > MaxJsonDepth)
					{
						return true;
					}
				}
				else if ((character == '}' || character == ']') && depth > 0)
				{
					--depth;
				}
			}
			return false;
		}

		// "[json.exception.parse_error.101] parse error at ..." becomes "parse error at ...".
		std::string StripExceptionPrefix(std::string_view message)
		{
			if (message.starts_with('['))
			{
				const size_t end = message.find("] ");
				if (end != std::string_view::npos)
				{
					message.remove_prefix(end + 2);
				}
			}
			return std::string(message);
		}

		std::string WithSource(std::string_view source, std::string_view message)
		{
			return source.empty() ? std::string(message) : std::format("{}: {}", source, message);
		}

		std::string EscapeKey(std::string_view key)
		{
			std::string escaped;
			escaped.reserve(key.size());
			for (const char character : key)
			{
				if (character == '~')
				{
					escaped += "~0";
				}
				else if (character == '/')
				{
					escaped += "~1";
				}
				else
				{
					escaped += character;
				}
			}
			return escaped;
		}
	}

	Result<Json> ParseJson(std::string_view text, std::string_view source)
	{
		if (ExceedsDepth(text))
		{
			return Error(WithSource(source, std::format("nesting is deeper than {} levels", MaxJsonDepth)));
		}
		try
		{
			return Json::parse(text.begin(), text.end());
		}
		catch (const std::exception& exception)
		{
			return Error(WithSource(source, StripExceptionPrefix(exception.what())));
		}
	}

	Result<std::string> DumpJson(const Json& document)
	{
		try
		{
			std::string text = document.dump(2, ' ', false, Json::error_handler_t::strict);
			text.push_back('\n');
			return text;
		}
		catch (const std::exception& exception)
		{
			return Error(StripExceptionPrefix(exception.what()));
		}
	}

	std::string JsonPathAppend(std::string_view path, std::string_view key)
	{
		return std::format("{}/{}", path, EscapeKey(key));
	}

	std::string JsonPathAppend(std::string_view path, size_t index)
	{
		return std::format("{}/{}", path, index);
	}

	Error MakePathError(std::string_view path, std::string_view message)
	{
		return Error(path.empty() ? std::string(message) : std::format("{}: {}", path, message));
	}

	Error AddSource(std::string_view source, const Error& error)
	{
		return source.empty() ? error : Error(std::format("{}: {}", source, error.Message), error.Code);
	}

	void LoadReport::Warn(std::string_view path, std::string message)
	{
		if (m_Warnings.size() < MaxStored)
		{
			m_Warnings.push_back({ std::string(path), std::move(message) });
		}
		else
		{
			++m_Suppressed;
		}
	}

	void LoadReport::Log(std::string_view source) const
	{
		const std::string prefix = source.empty() ? std::string() : std::format("{}: ", source);
		for (const Diagnostic& warning : m_Warnings)
		{
			if (warning.Path.empty())
			{
				GB_CORE_WARN("{}{}", prefix, warning.Message);
			}
			else
			{
				GB_CORE_WARN("{}{}: {}", prefix, warning.Path, warning.Message);
			}
		}
		if (m_Suppressed > 0)
		{
			GB_CORE_WARN("{}{} more warnings were not listed", prefix, m_Suppressed);
		}
	}
}
