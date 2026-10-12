#pragma once

#include <string_view>

namespace Gambit
{
	// Whether `text` is well-formed UTF-8: no overlong forms, no surrogates and no code points beyond U+10FFFF.
	// JSON text must be: parsers reject anything else, and serializing such a string would throw inside nlohmann::json.
	[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;
}
