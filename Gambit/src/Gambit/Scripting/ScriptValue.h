#pragma once

#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Gambit
{
	class Hasher;
	struct ScriptTableEntry;

	// A linear RGBA color of the script API (Lua's Color).
	struct ScriptColorValue
	{
		glm::vec4 Rgba{ 1.0f };

		friend bool operator==(const ScriptColorValue&, const ScriptColorValue&) noexcept = default;
	};

	// An entity of the script's world, by UUID (Lua's Entity).
	struct ScriptEntityValue
	{
		UUID Id;

		friend bool operator==(const ScriptEntityValue&, const ScriptEntityValue&) noexcept = default;
	};

	// A Lua table as plain data: its entries sorted by key (ScriptValue::CompareKeys), so equal tables have equal
	// entries in the same order whatever order Lua visits them in.
	using ScriptTable = std::vector<ScriptTableEntry>;

	// Plain data that crosses between Lua and the engine (D-041): script event payloads, which are part of the world
	// state (copied with it and hashed, D-017), and the results of ScriptEngine::Evaluate. Values are nil, booleans,
	// integers and floats (Lua keeps them apart), strings, vectors, quaternions, colors, entities and tables of these.
	// Functions, coroutines and other userdata are not data.
	struct ScriptValue
	{
		using Variant = std::variant<
			std::monostate, bool, int64_t, double, std::string, glm::vec2, glm::vec3, glm::vec4, glm::quat,
			ScriptColorValue, ScriptEntityValue, ScriptTable>;

		// How deep tables may nest: deeper ones (and tables that contain themselves) are refused when converted from
		// Lua.
		static constexpr size_t MaxDepth = 32;

		Variant Data;

		ScriptValue() = default;
		template<typename T>
			requires std::is_constructible_v<Variant, T&&>
		ScriptValue(T&& value)
			: Data(std::forward<T>(value))
		{}

		[[nodiscard]] bool IsNil() const noexcept { return std::holds_alternative<std::monostate>(Data); }
		template<typename T>
		[[nodiscard]] bool Is() const noexcept
		{
			return std::holds_alternative<T>(Data);
		}
		// Null when the value is not a T.
		template<typename T>
		[[nodiscard]] const T* TryGet() const noexcept
		{
			return std::get_if<T>(&Data);
		}
		// The value of a table entry with this string key, null when this is not a table or has no such entry.
		[[nodiscard]] const ScriptValue* Find(std::string_view key) const;

		// The order of table keys: by kind (booleans, integers, floats, strings), then by value (false before true,
		// numbers ascending, strings byte-wise). Other kinds are not valid keys.
		[[nodiscard]] static bool CompareKeys(const ScriptValue& a, const ScriptValue& b);
		[[nodiscard]] static bool IsValidKey(const ScriptValue& key) noexcept;

		friend bool operator==(const ScriptValue& a, const ScriptValue& b);
	};

	struct ScriptTableEntry
	{
		ScriptValue Key;
		ScriptValue Value;

		friend bool operator==(const ScriptTableEntry&, const ScriptTableEntry&) = default;
	};

	// Feeds the value to a state hash: its kind, then its bits (floats by bit pattern), tables entry by entry.
	void HashScriptValue(Hasher& hasher, const ScriptValue& value);

	// Lua-like text for messages and tests: 1, 1.5, "text", Vec3(1, 2, 3), { a = 1, [2] = true }.
	[[nodiscard]] std::string ToString(const ScriptValue& value);

	// An event that a script emitted (world:Emit(name, payload)), stored in the world's event queue like the events of
	// C++ systems (D-017): copied with the world and part of its state hash.
	struct ScriptEvent
	{
		std::string Name;
		ScriptValue Payload;
	};

	void HashEvent(Hasher& hasher, const ScriptEvent& event);
}
