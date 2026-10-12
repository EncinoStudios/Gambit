#include <doctest/doctest.h>

#include "Gambit/Core/Buffer.h"

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using Gambit::Buffer;
using Gambit::BufferView;

TEST_CASE("Core: Buffer starts empty")
{
	const Buffer buffer;
	CHECK(buffer.Empty());
	CHECK(buffer.Size() == 0);
	CHECK(buffer.Data() == nullptr);
	CHECK(buffer.View().Empty());
}

TEST_CASE("Core: Buffer Allocate zero-fills and replaces the contents")
{
	Buffer buffer(16);
	REQUIRE(buffer.Size() == 16);
	for (const std::byte byte : buffer.Bytes())
	{
		CHECK(byte == std::byte{ 0 });
	}

	buffer.Data()[3] = std::byte{ 0xAB };
	buffer.Allocate(8);
	CHECK(buffer.Size() == 8);
	CHECK(buffer.Data()[3] == std::byte{ 0 });

	buffer.Allocate(0);
	CHECK(buffer.Empty());
	CHECK(buffer.Data() == nullptr);
}

TEST_CASE("Core: Buffer is move-only and a moved-from buffer is empty")
{
	static_assert(!std::is_copy_constructible_v<Buffer>);
	static_assert(!std::is_copy_assignable_v<Buffer>);
	static_assert(std::is_nothrow_move_constructible_v<Buffer>);
	static_assert(std::is_nothrow_move_assignable_v<Buffer>);

	Buffer source(4);
	source.Data()[0] = std::byte{ 7 };
	const std::byte* const address = source.Data();

	Buffer moved(std::move(source));
	CHECK(moved.Size() == 4);
	CHECK(moved.Data() == address);
	CHECK(source.Empty());
	CHECK(source.Data() == nullptr);

	Buffer assigned(2);
	assigned = std::move(moved);
	CHECK(assigned.Data() == address);
	CHECK(assigned.Size() == 4);
	CHECK(moved.Empty());
}

TEST_CASE("Core: Buffer Release frees the memory")
{
	Buffer buffer(32);
	buffer.Release();
	CHECK(buffer.Empty());
	CHECK(buffer.Data() == nullptr);
	buffer.Release(); // harmless
}

TEST_CASE("Core: Buffer Copy produces an independent duplicate")
{
	const std::array<uint8_t, 5> bytes{ 1, 2, 3, 4, 5 };
	Buffer copy = Buffer::Copy(bytes.data(), bytes.size());
	REQUIRE(copy.Size() == 5);
	CHECK(std::memcmp(copy.Data(), bytes.data(), 5) == 0);

	copy.Data()[0] = std::byte{ 99 };
	CHECK(bytes[0] == 1);

	const Buffer second = Buffer::Copy(copy);
	CHECK(second.Data() != copy.Data());
	CHECK(std::memcmp(second.Data(), copy.Data(), 5) == 0);

	CHECK(Buffer::Copy(BufferView()).Empty());
}

TEST_CASE("Core: Buffer As reinterprets the start of the buffer")
{
	Buffer buffer(sizeof(uint32_t) * 2);
	*buffer.As<uint32_t>() = 0x01020304u;
	CHECK(buffer.As<uint32_t>()[0] == 0x01020304u);

	const Buffer& constant = buffer;
	CHECK(*constant.As<uint32_t>() == 0x01020304u);

	struct Pair
	{
		uint32_t First;
		uint32_t Second;
	};
	buffer.As<Pair>()->Second = 9;
	CHECK(buffer.As<uint32_t>()[1] == 9);
}

TEST_CASE("Core: BufferView refers to memory without owning it")
{
	const std::vector<uint8_t> bytes{ 10, 20, 30, 40 };
	const BufferView view(bytes.data(), bytes.size());
	CHECK(view.Size() == 4);
	CHECK(view.Data() == reinterpret_cast<const std::byte*>(bytes.data()));
	CHECK_FALSE(view.Empty());

	const BufferView middle = view.Subview(1, 2);
	CHECK(middle.Size() == 2);
	CHECK(middle.Data()[0] == std::byte{ 20 });
	CHECK(view.Subview(4, 0).Empty());

	const BufferView text = BufferView::FromText("hello");
	CHECK(text.Size() == 5);
	CHECK(text.Data()[1] == std::byte{ 'e' });

	const Buffer buffer = Buffer::Copy(view);
	const BufferView fromBuffer = buffer; // implicit
	CHECK(fromBuffer.Data() == buffer.Data());
	CHECK(fromBuffer.Size() == buffer.Size());

	const std::array<std::byte, 3> array{};
	CHECK(BufferView(std::span<const std::byte>(array)).Size() == 3);
}

TEST_CASE("Core: BufferView As reads a value at the start")
{
	const uint32_t values[2]{ 0xCAFEBABEu, 5u };
	const BufferView view(values, sizeof(values));
	CHECK(*view.As<uint32_t>() == 0xCAFEBABEu);
	CHECK(view.As<uint32_t>()[1] == 5u);
}
