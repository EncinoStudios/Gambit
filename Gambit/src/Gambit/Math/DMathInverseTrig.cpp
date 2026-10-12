// Deterministic asin, acos, atan and atan2 (D-011).
//
// Port of musl libc 1.2.6 src/math/{asin,acos,atan,atan2}.c, which derive from FreeBSD msun and Sun fdlibm.
//
// The code uses IEEE-754 arithmetic (+ - * / and the exactly rounded sqrt, floor and fabs), integer operations and
// bit casts only: no call into the platform libm, no fenv side effects and no fused multiply-add (the build compiles
// with -ffp-contract=off, and the FMA variants of the original were removed). Changes against the original: C++ port
// with Gambit naming, std::bit_cast instead of unions, canonical NaN results (Internal/DMathBits.h), and no
// rounding-mode, exception-flag or long double code paths.
//
// ---- Notices of the original source files ----
//
// origin: FreeBSD /usr/src/lib/msun/src/ (e_asin.c, e_acos.c, e_atan2.c)
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// origin: FreeBSD /usr/src/lib/msun/src/ (s_atan.c)
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
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

#include <cmath>

namespace Gambit::DMath
{
	using namespace Internal;

	namespace
	{
		constexpr double Pi = 3.1415926535897931160e+00;   // 0x400921FB, 0x54442D18
		constexpr double PiLo = 1.2246467991473531772e-16; // 0x3CA1A626, 0x33145C07

		// ---- asin and acos share the rational approximation R(x^2) of (asin(x) - x) / x^3 ----

		constexpr double Pio2Hi = 1.57079632679489655800e+00; // 0x3FF921FB, 0x54442D18
		constexpr double Pio2Lo = 6.12323399573676603587e-17; // 0x3C91A626, 0x33145C07
		constexpr double PS0 = 1.66666666666666657415e-01;    // 0x3FC55555, 0x55555555
		constexpr double PS1 = -3.25565818622400915405e-01;   // 0xBFD4D612, 0x03EB6F7D
		constexpr double PS2 = 2.01212532134862925881e-01;    // 0x3FC9C155, 0x0E884455
		constexpr double PS3 = -4.00555345006794114027e-02;   // 0xBFA48228, 0xB5688F3B
		constexpr double PS4 = 7.91534994289814532176e-04;    // 0x3F49EFE0, 0x7501B288
		constexpr double PS5 = 3.47933107596021167570e-05;    // 0x3F023DE1, 0x0DFDF709
		constexpr double QS1 = -2.40339491173441421878e+00;   // 0xC0033A27, 0x1C8A2D4B
		constexpr double QS2 = 2.02094576023350569471e+00;    // 0x40002AE5, 0x9C598AC8
		constexpr double QS3 = -6.88283971605453293030e-01;   // 0xBFE6066C, 0x1B8D0159
		constexpr double QS4 = 7.70381505559019352791e-02;    // 0x3FB3B8C5, 0xB12E9282

		double AsinRational(double z)
		{
			const double p = z * (PS0 + z * (PS1 + z * (PS2 + z * (PS3 + z * (PS4 + z * PS5)))));
			const double q = 1.0 + z * (QS1 + z * (QS2 + z * (QS3 + z * QS4)));
			return p / q;
		}

		// ---- atan ----

		constexpr double AtanHi[] = {
			4.63647609000806093515e-01, // atan(0.5)hi 0x3FDDAC67, 0x0561BB4F
			7.85398163397448278999e-01, // atan(1.0)hi 0x3FE921FB, 0x54442D18
			9.82793723247329054082e-01, // atan(1.5)hi 0x3FEF730B, 0xD281F69B
			1.57079632679489655800e+00, // atan(inf)hi 0x3FF921FB, 0x54442D18
		};

		constexpr double AtanLo[] = {
			2.26987774529616870924e-17, // atan(0.5)lo 0x3C7A2B7F, 0x222F65E2
			3.06161699786838301793e-17, // atan(1.0)lo 0x3C81A626, 0x33145C07
			1.39033110312309984516e-17, // atan(1.5)lo 0x3C700788, 0x7AF0CBBD
			6.12323399573676603587e-17, // atan(inf)lo 0x3C91A626, 0x33145C07
		};

		constexpr double AtanPoly[] = {
			3.33333333333329318027e-01,  // 0x3FD55555, 0x5555550D
			-1.99999999998764832476e-01, // 0xBFC99999, 0x9998EBC4
			1.42857142725034663711e-01,  // 0x3FC24924, 0x920083FF
			-1.11111104054623557880e-01, // 0xBFBC71C6, 0xFE231671
			9.09088713343650656196e-02,  // 0x3FB745CD, 0xC54C206E
			-7.69187620504482999495e-02, // 0xBFB3B0F2, 0xAF749A6D
			6.66107313738753120669e-02,  // 0x3FB10D66, 0xA0D03D51
			-5.83357013379057348645e-02, // 0xBFADDE2D, 0x52DEFD9A
			4.97687799461593236017e-02,  // 0x3FA97B4B, 0x24760DEB
			-3.65315727442169155270e-02, // 0xBFA2B444, 0x2C6A6C2F
			1.62858201153657823623e-02,  // 0x3F90AD3A, 0xE322DA11
		};
	}

	double Asin(double x)
	{
		const uint32_t hx = HighWord(x);
		const uint32_t ix = hx & 0x7fffffff;

		// |x| >= 1 or NaN
		if (ix >= 0x3ff00000)
		{
			if (((ix - 0x3ff00000) | LowWord(x)) == 0)
			{
				return x * Pio2Hi; // asin(+-1) = +-pi/2
			}
			return QuietNaN();
		}
		// |x| < 0.5
		if (ix < 0x3fe00000)
		{
			if (ix < 0x3e500000 && ix >= 0x00100000) // 0x1p-1022 <= |x| < 0x1p-26
			{
				return x;
			}
			return x + x * AsinRational(x * x);
		}
		// 1 > |x| >= 0.5
		const double z = (1 - std::fabs(x)) * 0.5;
		const double s = std::sqrt(z);
		const double r = AsinRational(z);
		if (ix >= 0x3fef3333) // |x| > 0.975
		{
			x = Pio2Hi - (2 * (s + s * r) - Pio2Lo);
		}
		else
		{
			// f + c = sqrt(z)
			const double f = WithLowWord(s, 0);
			const double c = (z - f * f) / (s + f);
			x = 0.5 * Pio2Hi - (2 * s * r - (Pio2Lo - 2 * c) - (0.5 * Pio2Hi - 2 * f));
		}
		return (hx >> 31) != 0 ? -x : x;
	}

	double Acos(double x)
	{
		const uint32_t hx = HighWord(x);
		const uint32_t ix = hx & 0x7fffffff;

		// |x| >= 1 or NaN
		if (ix >= 0x3ff00000)
		{
			if (((ix - 0x3ff00000) | LowWord(x)) == 0)
			{
				// acos(1) = 0, acos(-1) = pi
				return (hx >> 31) != 0 ? 2 * Pio2Hi : 0.0;
			}
			return QuietNaN();
		}
		// |x| < 0.5
		if (ix < 0x3fe00000)
		{
			if (ix <= 0x3c600000) // |x| < 2**-57
			{
				return Pio2Hi;
			}
			return Pio2Hi - (x - (Pio2Lo - x * AsinRational(x * x)));
		}
		// x < -0.5
		if ((hx >> 31) != 0)
		{
			const double z = (1.0 + x) * 0.5;
			const double s = std::sqrt(z);
			const double w = AsinRational(z) * s - Pio2Lo;
			return 2 * (Pio2Hi - (s + w));
		}
		// x > 0.5
		const double z = (1.0 - x) * 0.5;
		const double s = std::sqrt(z);
		const double df = WithLowWord(s, 0);
		const double c = (z - df * df) / (s + df);
		const double w = AsinRational(z) * s + c;
		return 2 * (df + w);
	}

	double Atan(double x)
	{
		const uint32_t hx = HighWord(x);
		const bool negative = (hx >> 31) != 0;
		const uint32_t ix = hx & 0x7fffffff;

		if (ix >= 0x44100000) // |x| >= 2^66
		{
			if (x != x)
			{
				return QuietNaN();
			}
			return negative ? -AtanHi[3] : AtanHi[3];
		}

		int id = 0;
		if (ix < 0x3fdc0000) // |x| < 0.4375
		{
			if (ix < 0x3e400000) // |x| < 2^-27
			{
				return x;
			}
			id = -1;
		}
		else
		{
			x = std::fabs(x);
			if (ix < 0x3ff30000) // |x| < 1.1875
			{
				if (ix < 0x3fe60000) // 7/16 <= |x| < 11/16
				{
					id = 0;
					x = (2.0 * x - 1.0) / (2.0 + x);
				}
				else // 11/16 <= |x| < 19/16
				{
					id = 1;
					x = (x - 1.0) / (x + 1.0);
				}
			}
			else if (ix < 0x40038000) // |x| < 2.4375
			{
				id = 2;
				x = (x - 1.5) / (1.0 + 1.5 * x);
			}
			else // 2.4375 <= |x| < 2^66
			{
				id = 3;
				x = -1.0 / x;
			}
		}

		// End of argument reduction. Sum from i = 0 to 10 of AtanPoly[i] * z^(i+1), as odd and even polynomials.
		const double* const aT = AtanPoly;
		double z = x * x;
		const double w = z * z;
		const double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
		const double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
		if (id < 0)
		{
			return x - x * (s1 + s2);
		}
		z = AtanHi[id] - (x * (s1 + s2) - AtanLo[id] - x);
		return negative ? -z : z;
	}

	double Atan2(double y, double x)
	{
		if (x != x || y != y)
		{
			return QuietNaN();
		}
		uint32_t ix = HighWord(x);
		const uint32_t lx = LowWord(x);
		uint32_t iy = HighWord(y);
		const uint32_t ly = LowWord(y);
		if (((ix - 0x3ff00000) | lx) == 0) // x = 1.0
		{
			return Atan(y);
		}
		const uint32_t m = ((iy >> 31) & 1) | ((ix >> 30) & 2); // 2*sign(x)+sign(y)
		ix = ix & 0x7fffffff;
		iy = iy & 0x7fffffff;

		// when y = 0
		if ((iy | ly) == 0)
		{
			if (m < 2)
			{
				return y; // atan(+-0, +anything) = +-0
			}
			return m == 2 ? Pi : -Pi; // atan(+-0, -anything) = +-pi
		}
		// when x = 0
		if ((ix | lx) == 0)
		{
			return (m & 1) != 0 ? -Pi / 2 : Pi / 2;
		}
		// when x is INF
		if (ix == 0x7ff00000)
		{
			if (iy == 0x7ff00000)
			{
				switch (m)
				{
					case 0:
						return Pi / 4; // atan(+INF, +INF)
					case 1:
						return -Pi / 4; // atan(-INF, +INF)
					case 2:
						return 3 * Pi / 4; // atan(+INF, -INF)
					default:
						return -3 * Pi / 4; // atan(-INF, -INF)
				}
			}
			switch (m)
			{
				case 0:
					return 0.0; // atan(+..., +INF)
				case 1:
					return -0.0; // atan(-..., +INF)
				case 2:
					return Pi; // atan(+..., -INF)
				default:
					return -Pi; // atan(-..., -INF)
			}
		}
		// |y/x| > 0x1p64
		if (ix + (64u << 20) < iy || iy == 0x7ff00000)
		{
			return (m & 1) != 0 ? -Pi / 2 : Pi / 2;
		}

		// z = atan(|y/x|) without spurious underflow
		double z = 0.0;
		if (!((m & 2) != 0 && iy + (64u << 20) < ix)) // not (|y/x| < 0x1p-64 with x < 0)
		{
			z = Atan(std::fabs(y / x));
		}
		switch (m)
		{
			case 0:
				return z; // atan(+,+)
			case 1:
				return -z; // atan(-,+)
			case 2:
				return Pi - (z - PiLo); // atan(+,-)
			default:
				return (z - PiLo) - Pi; // atan(-,-)
		}
	}
}
