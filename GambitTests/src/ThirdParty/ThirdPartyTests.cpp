// Smoke tests that every wired third-party target compiles, links and behaves as the pinned version documents.
// They are not tests of the libraries themselves; they catch broken CMake wiring and accidental version drift.

#include <doctest/doctest.h>

#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <lz4.h>
#include <lz4frame.h>
#include <monocypher-ed25519.h>
#include <monocypher.h>
#include <nlohmann/json.hpp>
#include <nvrhi/nvrhi.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <vulkan/vulkan_core.h>
#include <xxhash.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	std::string ToHex(const uint8_t* data, size_t size)
	{
		static constexpr std::string_view Digits = "0123456789abcdef";
		std::string hex;
		for (size_t i = 0; i < size; ++i)
		{
			hex += Digits[data[i] >> 4];
			hex += Digits[data[i] & 0xF];
		}
		return hex;
	}

	std::vector<uint8_t> FromHex(std::string_view hex)
	{
		std::vector<uint8_t> bytes;
		for (size_t i = 0; i + 1 < hex.size(); i += 2)
		{
			bytes.push_back(static_cast<uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
		}
		return bytes;
	}
}

TEST_CASE("ThirdParty: glm vector math")
{
	const glm::vec3 a{ 1.0f, 2.0f, 3.0f };
	const glm::vec3 b{ 4.0f, 5.0f, 6.0f };
	CHECK(glm::dot(a, b) == 32.0f);
	CHECK(glm::cross(a, b) == glm::vec3{ -3.0f, 6.0f, -3.0f });
}

TEST_CASE("ThirdParty: EnTT registry")
{
	entt::registry registry;
	const entt::entity first = registry.create();
	const entt::entity second = registry.create();
	registry.emplace<int>(first, 1);
	registry.emplace<int>(second, 2);

	int sum = 0;
	for (const auto [entity, value] : registry.view<int>().each())
	{
		static_cast<void>(entity);
		sum += value;
	}
	CHECK(sum == 3);
	CHECK(ENTT_VERSION_MAJOR == 3);
	CHECK(ENTT_VERSION_MINOR == 16);
}

TEST_CASE("ThirdParty: nlohmann json parses and dumps with sorted keys")
{
	const nlohmann::json parsed = nlohmann::json::parse(R"({"b":[1,2,3],"a":"x"})");
	CHECK(parsed["b"].size() == 3);
	CHECK(parsed["a"] == "x");
	// Key order of dump() is what keeps serialized scenes byte-stable.
	CHECK(parsed.dump() == R"({"a":"x","b":[1,2,3]})");
	CHECK(NLOHMANN_JSON_VERSION_MAJOR == 3);
	CHECK(NLOHMANN_JSON_VERSION_MINOR == 12);
}

TEST_CASE("ThirdParty: GLFW reports its pinned version without being initialized")
{
	int major = 0;
	int minor = 0;
	int revision = 0;
	glfwGetVersion(&major, &minor, &revision);
	CHECK(major == 3);
	CHECK(minor == 4);
	CHECK(revision == 0);
}

TEST_CASE("ThirdParty: xxHash matches the reference digests")
{
	CHECK(XXH64(nullptr, 0, 0) == 0xEF46DB3751D8E999ULL);
	CHECK(XXH3_64bits(nullptr, 0) == 0x2D06800538D394C2ULL);
}

TEST_CASE("ThirdParty: LZ4 round trip")
{
	CHECK(LZ4_versionNumber() == 11000); // 1.10.0

	std::string input;
	for (int i = 0; i < 200; ++i)
	{
		input += "gambit engine ";
	}

	std::vector<char> compressed(static_cast<size_t>(LZ4_compressBound(static_cast<int>(input.size()))));
	const int compressedSize = LZ4_compress_default(
		input.data(), compressed.data(), static_cast<int>(input.size()), static_cast<int>(compressed.size()));
	REQUIRE(compressedSize > 0);
	CHECK(static_cast<size_t>(compressedSize) < input.size() / 4);

	std::vector<char> restored(input.size());
	const int restoredSize =
		LZ4_decompress_safe(compressed.data(), restored.data(), compressedSize, static_cast<int>(restored.size()));
	REQUIRE(restoredSize == static_cast<int>(input.size()));
	CHECK(std::string(restored.data(), restored.size()) == input);
}

TEST_CASE("ThirdParty: LZ4 frames verify their content checksum with LZ4's private xxhash")
{
	// lz4frame.c hashes with LZ4's own (namespaced) copy of xxhash; it must not bind to xxHash's symbols.
	const std::string input(1000, 'g');
	LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
	preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;

	std::vector<char> frame(LZ4F_compressFrameBound(input.size(), &preferences));
	const size_t frameSize = LZ4F_compressFrame(frame.data(), frame.size(), input.data(), input.size(), &preferences);
	REQUIRE(LZ4F_isError(frameSize) == 0);
	frame.resize(frameSize);

	const auto decompress = [&input](const std::vector<char>& source) {
		LZ4F_dctx* context = nullptr;
		REQUIRE(LZ4F_isError(LZ4F_createDecompressionContext(&context, LZ4F_VERSION)) == 0);
		std::vector<char> output(input.size());
		size_t outputSize = output.size();
		size_t sourceSize = source.size();
		const size_t result = LZ4F_decompress(context, output.data(), &outputSize, source.data(), &sourceSize, nullptr);
		LZ4F_freeDecompressionContext(context);
		// 0 means the frame was decoded completely, checksum included.
		return result == 0 && std::string(output.data(), outputSize) == input;
	};

	CHECK(decompress(frame));
	std::vector<char> corrupted = frame;
	corrupted.back() = static_cast<char>(corrupted.back() ^ 1); // the last 4 bytes are the content checksum
	CHECK_FALSE(decompress(corrupted));
}

TEST_CASE("ThirdParty: Monocypher BLAKE2b matches the reference digest")
{
	const std::string_view message = "abc";
	std::array<uint8_t, 64> hash{};
	crypto_blake2b(hash.data(), hash.size(), reinterpret_cast<const uint8_t*>(message.data()), message.size());
	CHECK(
		ToHex(hash.data(), hash.size())
		== "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc95"
		   "18d38aa8dbf1925ab92386edd4009923");
}

TEST_CASE("ThirdParty: Monocypher Ed25519 matches RFC 8032 test vector 1")
{
	std::vector<uint8_t> seed = FromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
	std::array<uint8_t, 64> secretKey{};
	std::array<uint8_t, 32> publicKey{};
	crypto_ed25519_key_pair(secretKey.data(), publicKey.data(), seed.data());
	CHECK(
		ToHex(publicKey.data(), publicKey.size())
		== "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

	std::array<uint8_t, 64> signature{};
	crypto_ed25519_sign(signature.data(), secretKey.data(), nullptr, 0);
	CHECK(
		ToHex(signature.data(), signature.size())
		== "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
		   "d25bf5f0595bbe24655141438e7a100b");
	CHECK(crypto_ed25519_check(signature.data(), publicKey.data(), nullptr, 0) == 0);

	signature[0] = static_cast<uint8_t>(signature[0] ^ 1);
	CHECK(crypto_ed25519_check(signature.data(), publicKey.data(), nullptr, 0) != 0);
}

TEST_CASE("ThirdParty: stb headers are on the include path")
{
	CHECK(STBI_VERSION == 1);

	// The engine compiles stb with STBI_NO_STDIO / STBI_WRITE_NO_STDIO (all IO goes through the VFS, D-014), so the
	// in-memory API is what is used: one pixel through PNG and back.
	const std::array<uint8_t, 4> pixel = { 10, 200, 30, 255 };
	std::vector<uint8_t> png;
	const auto append = [](void* context, void* data, int size) {
		auto* bytes = static_cast<std::vector<uint8_t>*>(context);
		bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
	};
	REQUIRE(stbi_write_png_to_func(append, &png, 1, 1, 4, pixel.data(), 4) != 0);
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 4);
	REQUIRE(decoded != nullptr);
	CHECK(width == 1);
	CHECK(height == 1);
	CHECK(std::memcmp(decoded, pixel.data(), pixel.size()) == 0);
	stbi_image_free(decoded);
}

TEST_CASE("ThirdParty: Vulkan-Headers and NVRHI are the pinned versions")
{
	CHECK(VK_HEADER_VERSION == 352);
	CHECK(nvrhi::c_HeaderVersion == 27);
	CHECK(nvrhi::verifyHeaderVersion());
}
