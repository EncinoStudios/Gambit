#include "gbpch.h"

#include "Gambit/Core/Hash.h"

// The streaming state is only defined with this macro; Hasher embeds it by value to avoid an allocation per hash.
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

#include <new>

namespace Gambit
{
	static_assert(sizeof(XXH64_state_t) == 88, "Hasher::m_State must match XXH64_state_t");
	static_assert(alignof(XXH64_state_t) <= 8, "Hasher::m_State is not aligned for XXH64_state_t");

	namespace
	{
		XXH64_state_t* GetState(std::byte* storage) noexcept
		{
			return std::launder(reinterpret_cast<XXH64_state_t*>(storage));
		}

		const XXH64_state_t* GetState(const std::byte* storage) noexcept
		{
			return std::launder(reinterpret_cast<const XXH64_state_t*>(storage));
		}
	}

	uint64_t Hash::XXH64(const void* data, size_t size, uint64_t seed) noexcept
	{
		return ::XXH64(data, size, seed);
	}

	Hasher::Hasher(uint64_t seed) noexcept
	{
		::new (static_cast<void*>(m_State)) XXH64_state_t;
		Reset(seed);
	}

	void Hasher::Reset(uint64_t seed) noexcept
	{
		// XXH64_reset only fails for a null state.
		[[maybe_unused]] const XXH_errorcode result = XXH64_reset(GetState(m_State), seed);
		GB_CORE_ASSERT(result == XXH_OK, "XXH64_reset failed");
	}

	void Hasher::Update(const void* data, size_t size) noexcept
	{
		if (size == 0)
		{
			return; // XXH64_update rejects a null pointer even for an empty range
		}
		[[maybe_unused]] const XXH_errorcode result = XXH64_update(GetState(m_State), data, size);
		GB_CORE_ASSERT(result == XXH_OK, "XXH64_update failed");
	}

	uint64_t Hasher::Digest() const noexcept
	{
		return XXH64_digest(GetState(m_State));
	}
}
