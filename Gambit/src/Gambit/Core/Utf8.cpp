#include "gbpch.h"

#include "Gambit/Core/Utf8.h"

namespace Gambit
{
	bool IsValidUtf8(std::string_view text) noexcept
	{
		size_t index = 0;
		while (index < text.size())
		{
			const auto lead = static_cast<unsigned char>(text[index]);
			size_t length = 1;
			uint32_t codepoint = lead;
			if (lead >= 0xC2 && lead <= 0xDF)
			{
				length = 2;
				codepoint = lead & 0x1Fu;
			}
			else if (lead >= 0xE0 && lead <= 0xEF)
			{
				length = 3;
				codepoint = lead & 0x0Fu;
			}
			else if (lead >= 0xF0 && lead <= 0xF4)
			{
				length = 4;
				codepoint = lead & 0x07u;
			}
			else if (lead >= 0x80)
			{
				return false; // a continuation byte, or a lead byte of an overlong or out-of-range sequence
			}
			if (text.size() - index < length)
			{
				return false;
			}
			for (size_t offset = 1; offset < length; ++offset)
			{
				const auto continuation = static_cast<unsigned char>(text[index + offset]);
				if ((continuation & 0xC0u) != 0x80u)
				{
					return false;
				}
				codepoint = (codepoint << 6) | (continuation & 0x3Fu);
			}
			const bool overlong = (length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000);
			if (overlong || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
			{
				return false;
			}
			index += length;
		}
		return true;
	}
}
