#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Gambit
{
	// 64-bit identifier of entities and assets (D-021); 0 is the invalid (null) UUID. Serialized as "0x%016llX"
	// strings (D-025).
	class UUID
	{
	public:
		constexpr UUID() noexcept = default;
		constexpr explicit UUID(uint64_t value) noexcept
			: m_Value(value)
		{}

		// A random, non-zero UUID from the operating system's entropy source. Thread safe. For authoring tools only
		// (editor, importers): it is not reproducible, so simulation code must draw UUIDs from the world RNG (D-011).
		[[nodiscard]] static UUID Generate();

		// Accepts "0x" or "0X" followed by 1 to 16 hexadecimal digits (either case) and nothing else. The null
		// UUID parses successfully, so serialized null references round-trip; check IsValid() where it matters.
		[[nodiscard]] static std::optional<UUID> Parse(std::string_view text);

		[[nodiscard]] constexpr uint64_t GetValue() const noexcept { return m_Value; }
		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }

		// "0x" plus 16 upper-case hexadecimal digits, e.g. "0x00000000DEADBEEF".
		[[nodiscard]] std::string ToString() const;

		friend constexpr bool operator==(const UUID&, const UUID&) noexcept = default;
		friend constexpr auto operator<=>(const UUID&, const UUID&) noexcept = default;

	private:
		uint64_t m_Value = 0;
	};
}

template<>
struct std::hash<Gambit::UUID>
{
	// UUIDs are already uniformly distributed (random or drawn from the world RNG), so the value is its own hash.
	[[nodiscard]] size_t operator()(const Gambit::UUID& uuid) const noexcept
	{
		return static_cast<size_t>(uuid.GetValue());
	}
};
