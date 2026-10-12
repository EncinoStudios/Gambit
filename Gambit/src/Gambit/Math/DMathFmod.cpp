// Deterministic fmod (D-011).
//
// Port of musl libc 1.2.6 src/math/fmod.c, an exact integer-only remainder (the file has no separate notice).
//
// The code uses IEEE-754 arithmetic (+ - * / and the exactly rounded sqrt, floor and fabs), integer operations and
// bit casts only: no call into the platform libm, no fenv side effects and no fused multiply-add (the build compiles
// with -ffp-contract=off, and the FMA variants of the original were removed). Changes against the original: C++ port
// with Gambit naming, std::bit_cast instead of unions, canonical NaN results (Internal/DMathBits.h), and no
// rounding-mode, exception-flag or long double code paths.
//
// ---- musl license ----
//
// musl as a whole is licensed under the following standard MIT license:
//
// Copyright © 2005-2020 Rich Felker, et al.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "gbpch.h"

#include "Gambit/Math/DMath.h"

#include "Gambit/Math/Internal/DMathBits.h"

namespace Gambit::DMath
{
	using namespace Internal;

	double Fmod(double x, double y)
	{
		uint64_t uxi = AsBits(x);
		uint64_t uyi = AsBits(y);
		int ex = static_cast<int>((uxi >> 52) & 0x7ff);
		int ey = static_cast<int>((uyi >> 52) & 0x7ff);
		const uint64_t sx = uxi >> 63;

		if ((uyi << 1) == 0 || y != y || ex == 0x7ff)
		{
			return QuietNaN(); // fmod(x, 0), fmod(inf, y), fmod(x, NaN)
		}
		if ((uxi << 1) <= (uyi << 1))
		{
			if ((uxi << 1) == (uyi << 1))
			{
				return FromBits(sx << 63); // |x| == |y|: a zero with the sign of x
			}
			return x;
		}

		// Normalize x and y.
		uint64_t i = 0;
		if (ex == 0)
		{
			for (i = uxi << 12; (i >> 63) == 0; ex--, i <<= 1)
			{
			}
			uxi <<= -ex + 1;
		}
		else
		{
			uxi &= ~uint64_t{ 0 } >> 12;
			uxi |= 1ull << 52;
		}
		if (ey == 0)
		{
			for (i = uyi << 12; (i >> 63) == 0; ey--, i <<= 1)
			{
			}
			uyi <<= -ey + 1;
		}
		else
		{
			uyi &= ~uint64_t{ 0 } >> 12;
			uyi |= 1ull << 52;
		}

		// x mod y
		for (; ex > ey; ex--)
		{
			i = uxi - uyi;
			if ((i >> 63) == 0)
			{
				if (i == 0)
				{
					return FromBits(sx << 63);
				}
				uxi = i;
			}
			uxi <<= 1;
		}
		i = uxi - uyi;
		if ((i >> 63) == 0)
		{
			if (i == 0)
			{
				return FromBits(sx << 63);
			}
			uxi = i;
		}
		for (; (uxi >> 52) == 0; uxi <<= 1, ex--)
		{
		}

		// Scale the result.
		if (ex > 0)
		{
			uxi -= 1ull << 52;
			uxi |= static_cast<uint64_t>(ex) << 52;
		}
		else
		{
			uxi >>= -ex + 1;
		}
		uxi |= sx << 63;
		return FromBits(uxi);
	}
}
