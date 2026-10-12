#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Reflection/FieldType.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class Hasher;

	// Per-field behaviour flags (D-022).
	enum class FieldFlags : uint8_t
	{
		None = 0,
		Hidden = GB_BIT(0),        // not shown by the inspector (still serialized, hashed and scriptable)
		ReadOnly = GB_BIT(1),      // shown but not edited by tools (inspector, CLI, bulk editor); code may write it
		NotSerialized = GB_BIT(2), // runtime state that files do not store
		NotHashed = GB_BIT(3)      // derived or presentation data left out of the state hash (D-013)
	};

	[[nodiscard]] constexpr FieldFlags operator|(FieldFlags a, FieldFlags b) noexcept
	{
		return static_cast<FieldFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	// Whether every bit of `flag` is set.
	[[nodiscard]] constexpr bool HasFlag(FieldFlags flags, FieldFlags flag) noexcept
	{
		return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) == static_cast<uint8_t>(flag)
			&& flag != FieldFlags::None;
	}

	// Inclusive editing range of a numeric field (per component for vectors and colors). A hint for tools: values set
	// through code are not clamped.
	struct FieldRange
	{
		double Min = 0.0;
		double Max = 0.0;

		friend bool operator==(const FieldRange&, const FieldRange&) noexcept = default;
	};

	struct EnumEntry
	{
		std::string Name;
		int64_t Value = 0;

		friend bool operator==(const EnumEntry&, const EnumEntry&) noexcept = default;
	};

	// The named values of an enum field: from a C++ enum registered with TypeRegistry::RegisterEnum, or declared by a
	// script component schema. Names and values are unique.
	struct EnumInfo
	{
		std::string Name; // empty for the anonymous enums of script component fields
		std::vector<EnumEntry> Entries;

		[[nodiscard]] std::optional<int64_t> FindValue(std::string_view name) const noexcept;
		// Empty when `value` has no name.
		[[nodiscard]] std::string_view FindName(int64_t value) const noexcept;
		[[nodiscard]] bool Contains(int64_t value) const noexcept { return !FindName(value).empty(); }

		friend bool operator==(const EnumInfo&, const EnumInfo&) noexcept = default;
	};

	// Everything the tools know about a field beyond its type.
	struct FieldAttributes
	{
		FieldFlags Flags = FieldFlags::None;
		std::optional<FieldRange> Range;
		std::string Tooltip;
		std::string Category;     // inspector grouping, e.g. "Physics"
		std::string AssetType;    // AssetHandle fields (and arrays of them): the asset type they accept, e.g. "Texture"
		Ref<const EnumInfo> Enum; // Enum fields: their values

		[[nodiscard]] bool HasFlag(FieldFlags flag) const noexcept { return Gambit::HasFlag(Flags, flag); }

		// Compares the enum by content, not by identity.
		[[nodiscard]] bool IsEquivalent(const FieldAttributes& other) const noexcept;
	};

	// Attributes passed to ComponentBuilder::Field, e.g.
	// .Field("Speed", &MoverComponent::Speed, Attr::Range{ 0.0, 10.0 }, Attr::Tooltip{ "Units per second" }).
	namespace Attr
	{
		struct Range
		{
			double Min = 0.0;
			double Max = 0.0;
		};
		struct Tooltip
		{
			std::string_view Text;
		};
		struct Category
		{
			std::string_view Name;
		};
		// Makes a UUID field (or an array of UUIDs) an AssetHandle field that accepts assets of this type.
		struct AssetType
		{
			std::string_view Type;
		};
		// Makes a glm::vec3 / glm::vec4 field (or an array of them) a Color3 / Color4 field.
		struct Color
		{};
		struct Hidden
		{};
		struct ReadOnly
		{};
		struct NotSerialized
		{};
		struct NotHashed
		{};
	}

	namespace Internal
	{
		// How a field is reached inside a component. C++ fields live at a byte offset of the component struct; fields
		// of script components (DynamicComponent) are entries of its value array. Read, Write and Hash work on the
		// field's storage (see FieldInfo::GetRawPointer); Write is only called with a value IsValueOfType accepted.
		struct FieldAccess
		{
			size_t Offset = 0; // byte offset, or value index for DynamicComponent fields
			void* (*Locate)(void* component, size_t offset) = nullptr;
			FieldValue (*Read)(const void* storage) = nullptr;
			void (*Write)(void* storage, const FieldValue& value) = nullptr;
			void (*Hash)(Hasher& hasher, const void* storage) = nullptr;
		};
	}

	// One reflected field of a component (D-022). The component pointer passed to the accessors is the one
	// ComponentInfo::GetRaw returns.
	struct FieldInfo
	{
		std::string Name; // stable: used in files, Lua and the CLI
		FieldType Type = FieldType::None;
		FieldType ElementType = FieldType::None; // Array fields only
		FieldAttributes Attributes;
		FieldValue Default; // the value of a newly added component
		Internal::FieldAccess Access;

		[[nodiscard]] bool HasFlag(FieldFlags flag) const noexcept { return Attributes.HasFlag(flag); }

		// Whether SetValue accepts `value`: the right FieldValue alternative and, for Enum fields, one of the enum's
		// values.
		[[nodiscard]] bool CanHold(const FieldValue& value) const noexcept;

		[[nodiscard]] FieldValue GetValue(const void* component) const;
		// False, changing nothing, when CanHold(value) is false.
		[[nodiscard]] bool SetValue(void* component, const FieldValue& value) const;
		// Feeds the value to a state hash (D-013) without copying it; the encoding is the same for C++ and script
		// fields of the same type (Reflection/FieldHashing.h).
		void HashValue(Hasher& hasher, const void* component) const;

		// The field's storage: a T* for a field stored as T (the FieldValue alternative of its type). Enum fields are
		// the exception: C++ enum fields point to the enum itself, script ones to an int64_t; use GetValue/SetValue for
		// them.
		[[nodiscard]] void* GetRawPointer(void* component) const noexcept;
		[[nodiscard]] const void* GetRawPointer(const void* component) const noexcept;
	};
}
