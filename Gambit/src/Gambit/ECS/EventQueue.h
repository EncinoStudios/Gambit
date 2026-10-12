#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Hash.h"
#include "Gambit/Reflection/FieldHashing.h"

#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Gambit
{
	// A world event type: plain, copyable data plus a HashEvent overload, declared next to the type (it is found by
	// argument-dependent lookup), that feeds every field to the state hash (D-013, D-017):
	//   struct DamageEvent { UUID Target; float Amount = 0.0f; };
	//   inline void HashEvent(Hasher& hasher, const DamageEvent& event)
	//   {
	//       HashEventFields(hasher, event.Target, event.Amount);
	//   }
	template<typename T>
	concept WorldEvent = std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>
		&& requires(Hasher& hasher, const T& event) { HashEvent(hasher, event); };

	namespace Internal
	{
		template<typename F>
		void HashEventField(Hasher& hasher, const F& field)
		{
			if constexpr (std::is_enum_v<F>)
			{
				hasher.UpdateValue(static_cast<int64_t>(static_cast<std::underlying_type_t<F>>(field)));
			}
			else
			{
				HashStored<F>(hasher, &field);
			}
		}

		// Type-erased part of an EventQueue, so the World can swap, clear, copy and hash queues of every event type.
		class IEventQueue
		{
		public:
			virtual ~IEventQueue() = default;
			// Starts a tick: the current events become the previous tick's, and the current buffer empties.
			virtual void Swap() noexcept = 0;
			virtual void Clear() noexcept = 0;
			// Neither buffer holds an event.
			[[nodiscard]] virtual bool IsEmpty() const noexcept = 0;
			// Both buffers: the previous tick's events, then the current ones.
			virtual void Hash(Hasher& hasher) const = 0;
			[[nodiscard]] virtual Scope<IEventQueue> Clone() const = 0;
		};
	}

	// Feeds event fields to a hash with the encoding of reflected fields (Reflection/FieldHashing.h): bool, integers,
	// floating point (by bit pattern), glm vectors and quaternions, std::string, UUID, enums (as int64) and std::vector
	// of any of these except enums.
	template<typename... Fields>
	void HashEventFields(Hasher& hasher, const Fields&... fields)
	{
		(Internal::HashEventField(hasher, fields), ...);
	}

	// The events of type T of a World (World::Events<T>()), double-buffered (D-017). Emit adds to the current tick's
	// events; at the start of every tick the World turns them into the previous tick's. View() is the complete set the
	// previous tick emitted and stays unchanged for the whole tick, so every system sees each event exactly once,
	// whatever its phase; ViewCurrent() is what this tick emitted so far, for readers that run after the emitter in the
	// same tick (e.g. PostPhysics reading collisions). Events emitted between ticks are part of the next tick's View().
	// Both buffers are copied by World::Copy and hashed by World::ComputeStateHash.
	template<WorldEvent T>
	class EventQueue final : public Internal::IEventQueue
	{
	public:
		void Emit(const T& event) { m_Current.push_back(event); }
		void Emit(T&& event) { m_Current.push_back(std::move(event)); }

		// Builds the event from `args` by list initialization, so a plain-data event needs no constructor. Not
		// emplace_back(args...): that relies on parenthesized aggregate initialization, which Apple Clang 15 lacks.
		template<typename... Args>
		T& Emplace(Args&&... args)
		{
			return m_Current.emplace_back(T{ std::forward<Args>(args)... });
		}

		// The events of the previous tick, in emission order. Emitting does not invalidate it.
		[[nodiscard]] std::span<const T> View() const noexcept { return m_Previous; }
		// The events of the current tick so far, in emission order. Invalidated by Emit and Emplace: to emit while
		// reading it, iterate by index.
		[[nodiscard]] std::span<const T> ViewCurrent() const noexcept { return m_Current; }

		// Drops the events of both buffers.
		void Clear() noexcept override
		{
			m_Previous.clear();
			m_Current.clear();
		}

	private:
		void Swap() noexcept override
		{
			std::swap(m_Previous, m_Current); // keeps both allocations for the ticks to come
			m_Current.clear();
		}

		[[nodiscard]] bool IsEmpty() const noexcept override { return m_Previous.empty() && m_Current.empty(); }

		void Hash(Hasher& hasher) const override
		{
			for (const std::vector<T>* events : { &m_Previous, &m_Current })
			{
				hasher.UpdateValue(static_cast<uint64_t>(events->size()));
				for (const T& event : *events)
				{
					HashEvent(hasher, event);
				}
			}
		}

		[[nodiscard]] Scope<Internal::IEventQueue> Clone() const override
		{
			auto copy = CreateScope<EventQueue<T>>();
			copy->m_Previous = m_Previous;
			copy->m_Current = m_Current;
			return copy;
		}

		std::vector<T> m_Previous;
		std::vector<T> m_Current;
	};
}
