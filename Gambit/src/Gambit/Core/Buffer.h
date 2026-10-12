#pragma once

#include "Gambit/Core/Assert.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Gambit
{
	class Buffer;

	// A read-only, non-owning window onto bytes owned by someone else (a Buffer, a std::vector, a string literal).
	// It must not outlive the memory it refers to.
	class BufferView
	{
	public:
		constexpr BufferView() noexcept = default;
		constexpr BufferView(const void* data, size_t size) noexcept
			: m_Data(static_cast<const std::byte*>(data)), m_Size(size)
		{}
		constexpr BufferView(std::span<const std::byte> bytes) noexcept
			: m_Data(bytes.data()), m_Size(bytes.size())
		{}
		inline BufferView(const Buffer& buffer) noexcept;

		// The bytes of a string (without a terminator).
		[[nodiscard]] static constexpr BufferView FromText(std::string_view text) noexcept
		{
			return BufferView(text.data(), text.size());
		}

		[[nodiscard]] constexpr const std::byte* Data() const noexcept { return m_Data; }
		[[nodiscard]] constexpr size_t Size() const noexcept { return m_Size; }
		[[nodiscard]] constexpr bool Empty() const noexcept { return m_Size == 0; }
		[[nodiscard]] constexpr std::span<const std::byte> Bytes() const noexcept { return { m_Data, m_Size }; }

		// The `size` bytes starting at `offset`; the range must lie inside the view.
		[[nodiscard]] BufferView Subview(size_t offset, size_t size) const
		{
			GB_CORE_ASSERT(
				offset <= m_Size && size <= m_Size - offset, "subview [{}, +{}) exceeds {} bytes", offset, size,
				m_Size);
			return BufferView(m_Data + offset, size);
		}

		// Reinterprets the start of the view as a T. The view must hold at least sizeof(T) bytes and be suitably
		// aligned for T; use std::memcpy for unaligned data.
		template<typename T>
		[[nodiscard]] const T* As() const
		{
			static_assert(std::is_trivially_copyable_v<T>, "As<T> requires a trivially copyable type");
			GB_CORE_ASSERT(m_Size >= sizeof(T), "view of {} bytes is too small for a {}-byte type", m_Size, sizeof(T));
			GB_CORE_ASSERT(
				reinterpret_cast<std::uintptr_t>(m_Data) % alignof(T) == 0,
				"view is misaligned for the requested type");
			return reinterpret_cast<const T*>(m_Data);
		}

	private:
		const std::byte* m_Data = nullptr;
		size_t m_Size = 0;
	};

	// An owning, zero-initialized block of bytes (file contents, GPU upload staging, serialized data). Move-only so
	// that copying memory is always explicit (Buffer::Copy).
	class Buffer
	{
	public:
		Buffer() noexcept = default;
		explicit Buffer(size_t size) { Allocate(size); }
		Buffer(Buffer&& other) noexcept
			: m_Data(std::move(other.m_Data)), m_Size(std::exchange(other.m_Size, 0))
		{}
		Buffer& operator=(Buffer&& other) noexcept
		{
			if (this != &other)
			{
				m_Data = std::move(other.m_Data);
				m_Size = std::exchange(other.m_Size, 0);
			}
			return *this;
		}
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;
		~Buffer() = default;

		// Replaces the contents with `size` zero bytes. Throws std::bad_alloc like any allocation; code that sizes
		// a buffer from untrusted input must validate the size first.
		void Allocate(size_t size)
		{
			Release();
			if (size > 0)
			{
				m_Data = std::make_unique<std::byte[]>(size);
				m_Size = size;
			}
		}

		void Release() noexcept
		{
			m_Data.reset();
			m_Size = 0;
		}

		// A new buffer holding a copy of `source`.
		[[nodiscard]] static Buffer Copy(BufferView source)
		{
			Buffer copy(source.Size());
			if (!source.Empty())
			{
				std::memcpy(copy.Data(), source.Data(), source.Size());
			}
			return copy;
		}

		[[nodiscard]] static Buffer Copy(const void* data, size_t size) { return Copy(BufferView(data, size)); }

		[[nodiscard]] std::byte* Data() noexcept { return m_Data.get(); }
		[[nodiscard]] const std::byte* Data() const noexcept { return m_Data.get(); }
		[[nodiscard]] size_t Size() const noexcept { return m_Size; }
		[[nodiscard]] bool Empty() const noexcept { return m_Size == 0; }
		[[nodiscard]] std::span<std::byte> Bytes() noexcept { return { m_Data.get(), m_Size }; }
		[[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return { m_Data.get(), m_Size }; }
		[[nodiscard]] BufferView View() const noexcept { return BufferView(m_Data.get(), m_Size); }

		// Reinterprets the start of the buffer as a T. The buffer must hold at least sizeof(T) bytes. Allocations
		// are aligned for any fundamental type, so types with a larger alignment are rejected at compile time.
		template<typename T>
		[[nodiscard]] T* As() noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>, "As<T> requires a trivially copyable type");
			static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "type is over-aligned for a Buffer");
			GB_CORE_ASSERT(
				m_Size >= sizeof(T), "buffer of {} bytes is too small for a {}-byte type", m_Size, sizeof(T));
			return reinterpret_cast<T*>(m_Data.get());
		}

		template<typename T>
		[[nodiscard]] const T* As() const noexcept
		{
			return View().As<T>();
		}

	private:
		std::unique_ptr<std::byte[]> m_Data;
		size_t m_Size = 0;
	};

	inline BufferView::BufferView(const Buffer& buffer) noexcept
		: m_Data(buffer.Data()), m_Size(buffer.Size())
	{}
}
