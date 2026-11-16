#include "gbpch.h"

#include "Gambit/Scripting/ScriptValue.h"

#include "Gambit/Core/Hash.h"
#include "Gambit/Reflection/FieldHashing.h"

namespace Gambit
{
	namespace
	{
		// The rank of a key kind in CompareKeys.
		int KeyRank(const ScriptValue& key) noexcept
		{
			if (key.Is<bool>())
			{
				return 0;
			}
			if (key.Is<int64_t>())
			{
				return 1;
			}
			if (key.Is<double>())
			{
				return 2;
			}
			return 3;
		}

		bool IsIdentifier(std::string_view text) noexcept
		{
			const auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
			return !text.empty() && isLetter(text.front())
				&& std::ranges::all_of(text, [&](char c) { return isLetter(c) || (c >= '0' && c <= '9'); });
		}

		void AppendNumber(std::string& out, double value)
		{
			if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e15)
			{
				out += std::format("{:.1f}", value); // as Lua writes floats with an integral value: 2.0
				return;
			}
			out += std::format("{}", value);
		}

		// A Lua string literal: quotes, backslashes and control characters escaped.
		void AppendQuoted(std::string& out, std::string_view text)
		{
			out += '"';
			for (const char character : text)
			{
				switch (character)
				{
					case '"':
						out += "\\\"";
						break;
					case '\\':
						out += "\\\\";
						break;
					case '\n':
						out += "\\n";
						break;
					default:
						if (static_cast<unsigned char>(character) < 0x20)
						{
							out += std::format("\\{:03}", static_cast<int>(static_cast<unsigned char>(character)));
						}
						else
						{
							out += character;
						}
						break;
				}
			}
			out += '"';
		}

		void AppendVector(std::string& out, std::string_view name, std::span<const float> parts)
		{
			out += name;
			out += '(';
			for (size_t index = 0; index < parts.size(); ++index)
			{
				out += index == 0 ? "" : ", ";
				out += std::format("{}", parts[index]);
			}
			out += ')';
		}

		void Append(std::string& out, const ScriptValue& value);

		void AppendTable(std::string& out, const ScriptTable& table)
		{
			out += '{';
			for (size_t index = 0; index < table.size(); ++index)
			{
				const ScriptTableEntry& entry = table[index];
				out += index == 0 ? " " : ", ";
				if (const std::string* name = entry.Key.TryGet<std::string>(); name != nullptr && IsIdentifier(*name))
				{
					out += *name;
				}
				else
				{
					out += '[';
					Append(out, entry.Key);
					out += ']';
				}
				out += " = ";
				Append(out, entry.Value);
			}
			out += table.empty() ? "}" : " }";
		}

		void Append(std::string& out, const ScriptValue& value)
		{
			std::visit(
				[&]<typename T>(const T& data) {
					if constexpr (std::is_same_v<T, std::monostate>)
					{
						out += "nil";
					}
					else if constexpr (std::is_same_v<T, bool>)
					{
						out += data ? "true" : "false";
					}
					else if constexpr (std::is_same_v<T, int64_t>)
					{
						out += std::format("{}", data);
					}
					else if constexpr (std::is_same_v<T, double>)
					{
						AppendNumber(out, data);
					}
					else if constexpr (std::is_same_v<T, std::string>)
					{
						AppendQuoted(out, data);
					}
					else if constexpr (std::is_same_v<T, glm::vec2>)
					{
						AppendVector(out, "Vec2", std::span<const float>(&data.x, 2));
					}
					else if constexpr (std::is_same_v<T, glm::vec3>)
					{
						AppendVector(out, "Vec3", std::span<const float>(&data.x, 3));
					}
					else if constexpr (std::is_same_v<T, glm::vec4>)
					{
						AppendVector(out, "Vec4", std::span<const float>(&data.x, 4));
					}
					else if constexpr (std::is_same_v<T, glm::quat>)
					{
						const std::array<float, 4> parts = { data.x, data.y, data.z, data.w };
						AppendVector(out, "Quat", parts);
					}
					else if constexpr (std::is_same_v<T, ScriptColorValue>)
					{
						AppendVector(out, "Color", std::span<const float>(&data.Rgba.x, 4));
					}
					else if constexpr (std::is_same_v<T, ScriptEntityValue>)
					{
						out += std::format("Entity({})", data.Id.ToString());
					}
					else
					{
						AppendTable(out, data);
					}
				},
				value.Data);
		}

		// Kinds are hashed by their index in the variant: part of the hash format, so the alternatives keep their
		// order.
		void HashVector(Hasher& hasher, std::span<const float> parts)
		{
			for (const float part : parts)
			{
				hasher.UpdateValue(part);
			}
		}
	}

	const ScriptValue* ScriptValue::Find(std::string_view key) const
	{
		const ScriptTable* table = TryGet<ScriptTable>();
		if (table == nullptr)
		{
			return nullptr;
		}
		for (const ScriptTableEntry& entry : *table)
		{
			if (const std::string* name = entry.Key.TryGet<std::string>(); name != nullptr && *name == key)
			{
				return &entry.Value;
			}
		}
		return nullptr;
	}

	bool ScriptValue::IsValidKey(const ScriptValue& key) noexcept
	{
		if (const double* number = key.TryGet<double>(); number != nullptr)
		{
			return !std::isnan(*number); // Lua refuses NaN keys
		}
		return key.Is<bool>() || key.Is<int64_t>() || key.Is<std::string>();
	}

	bool ScriptValue::CompareKeys(const ScriptValue& a, const ScriptValue& b)
	{
		const int rankA = KeyRank(a);
		const int rankB = KeyRank(b);
		if (rankA != rankB)
		{
			return rankA < rankB;
		}
		if (const bool* flag = a.TryGet<bool>(); flag != nullptr)
		{
			return !*flag && *b.TryGet<bool>();
		}
		if (const int64_t* integer = a.TryGet<int64_t>(); integer != nullptr)
		{
			return *integer < *b.TryGet<int64_t>();
		}
		if (const double* number = a.TryGet<double>(); number != nullptr)
		{
			return *number < *b.TryGet<double>();
		}
		const std::string* textA = a.TryGet<std::string>();
		const std::string* textB = b.TryGet<std::string>();
		GB_CORE_ASSERT(textA != nullptr && textB != nullptr, "CompareKeys needs valid keys");
		return *textA < *textB;
	}

	bool operator==(const ScriptValue& a, const ScriptValue& b)
	{
		return a.Data == b.Data;
	}

	void HashScriptValue(Hasher& hasher, const ScriptValue& value)
	{
		hasher.UpdateValue(static_cast<uint32_t>(value.Data.index()));
		std::visit(
			[&]<typename T>(const T& data) {
				if constexpr (std::is_same_v<T, std::monostate>)
				{
				}
				else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t> || std::is_same_v<T, double>)
				{
					hasher.UpdateValue(data);
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					Internal::HashText(hasher, data);
				}
				else if constexpr (
					std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
				{
					HashVector(hasher, std::span<const float>(&data.x, T::length()));
				}
				else if constexpr (std::is_same_v<T, glm::quat>)
				{
					const std::array<float, 4> parts = { data.x, data.y, data.z, data.w };
					HashVector(hasher, parts);
				}
				else if constexpr (std::is_same_v<T, ScriptColorValue>)
				{
					HashVector(hasher, std::span<const float>(&data.Rgba.x, 4));
				}
				else if constexpr (std::is_same_v<T, ScriptEntityValue>)
				{
					hasher.UpdateValue(data.Id.GetValue());
				}
				else
				{
					hasher.UpdateValue(static_cast<uint64_t>(data.size()));
					for (const ScriptTableEntry& entry : data)
					{
						HashScriptValue(hasher, entry.Key);
						HashScriptValue(hasher, entry.Value);
					}
				}
			},
			value.Data);
	}

	std::string ToString(const ScriptValue& value)
	{
		std::string out;
		Append(out, value);
		return out;
	}

	void HashEvent(Hasher& hasher, const ScriptEvent& event)
	{
		Internal::HashText(hasher, event.Name);
		HashScriptValue(hasher, event.Payload);
	}
}
