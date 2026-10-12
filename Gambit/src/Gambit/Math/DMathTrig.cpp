// Deterministic sin, cos and tan (D-011).
//
// Port of musl libc 1.2.6 src/math/{sin,cos,tan,__sin,__cos,__tan,__rem_pio2,__rem_pio2_large}.c, which derive
// from FreeBSD msun and Sun fdlibm.
//
// The code uses IEEE-754 arithmetic (+ - * / and the exactly rounded sqrt, floor and fabs), integer operations and
// bit casts only: no call into the platform libm, no fenv side effects and no fused multiply-add (the build compiles
// with -ffp-contract=off, and the FMA variants of the original were removed). Changes against the original: C++ port
// with Gambit naming, std::bit_cast instead of unions, canonical NaN results (Internal/DMathBits.h), and no
// rounding-mode, exception-flag or long double code paths.
// The 2/pi table keeps the 66 chunks used for double precision.
//
// ---- Notices of the original source files ----
//
// origin: FreeBSD /usr/src/lib/msun/src/ (s_sin.c, s_cos.c, s_tan.c)
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// origin: FreeBSD /usr/src/lib/msun/src/ (k_sin.c, k_cos.c, e_rem_pio2.c, k_rem_pio2.c)
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// e_rem_pio2.c was optimized by Bruce D. Evans.
//
// origin: FreeBSD /usr/src/lib/msun/src/k_tan.c
// ====================================================
// Copyright 2004 Sun Microsystems, Inc.  All Rights Reserved.
//
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
		// ---- kernel sine on [-pi/4, pi/4] (k_sin.c) ----

		constexpr double S1 = -1.66666666666666324348e-01; // 0xBFC55555, 0x55555549
		constexpr double S2 = 8.33333333332248946124e-03;  // 0x3F811111, 0x1110F8A6
		constexpr double S3 = -1.98412698298579493134e-04; // 0xBF2A01A0, 0x19C161D5
		constexpr double S4 = 2.75573137070700676789e-06;  // 0x3EC71DE3, 0x57B1FE7D
		constexpr double S5 = -2.50507602534068634195e-08; // 0xBE5AE5E6, 0x8A2B9CEB
		constexpr double S6 = 1.58969099521155010221e-10;  // 0x3DE5D93A, 0x5ACFD57C

		// x is the head and y the tail of the argument; hasTail tells whether y may be non-zero.
		double KernelSin(double x, double y, bool hasTail)
		{
			const double z = x * x;
			const double w = z * z;
			const double r = S2 + z * (S3 + z * S4) + z * w * (S5 + z * S6);
			const double v = z * x;
			if (!hasTail)
			{
				return x + v * (S1 + z * r);
			}
			return x - ((z * (0.5 * y - v * r) - y) - v * S1);
		}

		// ---- kernel cosine on [-pi/4, pi/4] (k_cos.c) ----

		constexpr double C1 = 4.16666666666666019037e-02;  // 0x3FA55555, 0x5555554C
		constexpr double C2 = -1.38888888888741095749e-03; // 0xBF56C16C, 0x16C15177
		constexpr double C3 = 2.48015872894767294178e-05;  // 0x3EFA01A0, 0x19CB1590
		constexpr double C4 = -2.75573143513906633035e-07; // 0xBE927E4F, 0x809C52AD
		constexpr double C5 = 2.08757232129817482790e-09;  // 0x3E21EE9E, 0xBDB4B1C4
		constexpr double C6 = -1.13596475577881948265e-11; // 0xBDA8FAE9, 0xBE8838D4

		double KernelCos(double x, double y)
		{
			double z = x * x;
			double w = z * z;
			const double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
			const double hz = 0.5 * z;
			w = 1.0 - hz;
			return w + (((1.0 - w) - hz) + (z * r - x * y));
		}

		// ---- kernel tangent on [-pi/4, pi/4] (k_tan.c) ----

		constexpr double TanCoefficients[] = {
			3.33333333333334091986e-01,  // 3FD55555, 55555563
			1.33333333333201242699e-01,  // 3FC11111, 1110FE7A
			5.39682539762260521377e-02,  // 3FABA1BA, 1BB341FE
			2.18694882948595424599e-02,  // 3F9664F4, 8406D637
			8.86323982359930005737e-03,  // 3F8226E3, E96E8493
			3.59207910759131235356e-03,  // 3F6D6D22, C9560328
			1.45620945432529025516e-03,  // 3F57DBC8, FEE08315
			5.88041240820264096874e-04,  // 3F4344D8, F2F26501
			2.46463134818469906812e-04,  // 3F3026F7, 1A8D1068
			7.81794442939557092300e-05,  // 3F147E88, A03792A6
			7.14072491382608190305e-05,  // 3F12B80F, 32F0A7E9
			-1.85586374855275456654e-05, // BEF375CB, DB605373
			2.59073051863633712884e-05,  // 3EFB2A70, 74BF7AD4
		};
		constexpr double Pio4 = 7.85398163397448278999e-01;   // 3FE921FB, 54442D18
		constexpr double Pio4Lo = 3.06161699786838301793e-17; // 3C81A626, 33145C07

		// Returns tan(x + y), or -1/tan(x + y) when odd is set.
		double KernelTan(double x, double y, bool odd)
		{
			const double* const T = TanCoefficients;
			const bool big = (HighWord(x) & 0x7fffffff) >= 0x3FE59428; // |x| >= 0.6744
			bool negative = false;
			if (big)
			{
				negative = (HighWord(x) >> 31) != 0;
				if (negative)
				{
					x = -x;
					y = -y;
				}
				x = (Pio4 - x) + (Pio4Lo - y);
				y = 0.0;
			}
			const double z = x * x;
			double w = z * z;
			// Break x^5*(T[1]+x^2*T[2]+...) into x^5(T[1]+x^4*T[3]+...+x^20*T[11]) +
			// x^5(x^2*(T[2]+x^4*T[4]+...+x^22*[T12])).
			double r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
			double v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
			double s = z * x;
			r = y + z * (s * (r + v) + y) + s * T[0];
			w = x + r;
			if (big)
			{
				s = static_cast<double>(1 - 2 * static_cast<int>(odd));
				v = s - 2.0 * (x + (r - w * w / (w + s)));
				return negative ? -v : v;
			}
			if (!odd)
			{
				return w;
			}
			// -1.0/(x+r) has up to 2ulp error, so compute it accurately.
			const double w0 = WithLowWord(w, 0);
			v = r - (w0 - x); // w0+v = r+x
			const double a = -1.0 / w;
			const double a0 = WithLowWord(a, 0);
			return a0 + a * (1.0 + a0 * w0 + a0 * v);
		}

		// ---- argument reduction for huge arguments (k_rem_pio2.c) ----

		// 2/pi in 24-bit chunks: ipio2[i] * 2^(-24 (i+1)). 66 chunks cover every double (e0 <= 1000, jk = 4).
		constexpr int32_t TwoOverPi[] = {
			0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C, 0x439041, 0xFE5163, 0xABDEBB,
			0xC561B7, 0x246E3A, 0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129, 0xA73EE8, 0x8235F5,
			0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41, 0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
			0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF, 0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D,
			0x7527BA, 0xC7EBE5, 0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08, 0x560330, 0x46FC7B,
			0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3, 0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
			0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
		};

		// pi/2 cut into 24-bit chunks.
		constexpr double PiOver2Chunks[] = {
			1.57079625129699707031e+00, // 0x3FF921FB, 0x40000000
			7.54978941586159635335e-08, // 0x3E74442D, 0x00000000
			5.39030252995776476554e-15, // 0x3CF84698, 0x80000000
			3.28200341580791294123e-22, // 0x3B78CC51, 0x60000000
			1.27065575308067607349e-29, // 0x39F01B83, 0x80000000
			1.22933308981111328932e-36, // 0x387A2520, 0x40000000
			2.73370053816464559624e-44, // 0x36E38222, 0x80000000
			2.16741683877804819444e-51, // 0x3569F31D, 0x00000000
		};

		constexpr double Two24 = 16777216.0;                  // 2^24
		constexpr double TwoMinus24 = 5.9604644775390625e-08; // 2^-24

		// The scaled fractional part of x * 2/pi, for double precision (musl's __rem_pio2_large with prec = 1).
		// x[0..nx-1] are 24-bit pieces of the argument whose scaled exponent is e0; y receives the remainder
		// x - n*pi/2 as the sum y[0] + y[1], and the result is n mod 8.
		int ReducePiOver2Large(const double* x, double* y, int e0, int nx)
		{
			constexpr int32_t jk = 4; // jk+1 terms of 2/pi are enough for double precision
			constexpr int32_t jp = jk;
			double fq[20];
			double q[20];
			double f[20];
			int32_t iq[20];

			// Determine jx, jv, q0; note that 3 > q0.
			const int32_t jx = nx - 1;
			int32_t jv = (e0 - 3) / 24;
			if (jv < 0)
			{
				jv = 0;
			}
			int32_t q0 = e0 - 24 * (jv + 1);

			// Set up f[0] to f[jx+jk] where f[jx+jk] = TwoOverPi[jv+jk].
			int32_t j = jv - jx;
			const int32_t m = jx + jk;
			for (int32_t i = 0; i <= m; i++, j++)
			{
				f[i] = j < 0 ? 0.0 : static_cast<double>(TwoOverPi[j]);
			}

			// Compute q[0], q[1], ... q[jk].
			for (int32_t i = 0; i <= jk; i++)
			{
				double fw = 0.0;
				for (j = 0; j <= jx; j++)
				{
					fw += x[j] * f[jx + i - j];
				}
				q[i] = fw;
			}

			int32_t jz = jk;
			int32_t n = 0;
			int32_t ih = 0;
			double z = 0.0;
			for (;;) // recompute: more terms of 2/pi are added when the result suffers from cancellation
			{
				// Distill q[] into iq[] reversingly.
				z = q[jz];
				for (int32_t i = 0, jj = jz; jj > 0; i++, jj--)
				{
					const double fw = static_cast<double>(static_cast<int32_t>(TwoMinus24 * z));
					iq[i] = static_cast<int32_t>(z - Two24 * fw);
					z = q[jj - 1] + fw;
				}

				// Compute n.
				z = ScaleByPowerOfTwo(z, q0);     // actual value of z
				z -= 8.0 * std::floor(z * 0.125); // trim off integer >= 8
				n = static_cast<int32_t>(z);
				z -= static_cast<double>(n);
				ih = 0;
				if (q0 > 0) // need iq[jz-1] to determine n
				{
					const int32_t i = iq[jz - 1] >> (24 - q0);
					n += i;
					iq[jz - 1] -= i << (24 - q0);
					ih = iq[jz - 1] >> (23 - q0);
				}
				else if (q0 == 0)
				{
					ih = iq[jz - 1] >> 23;
				}
				else if (z >= 0.5)
				{
					ih = 2;
				}

				if (ih > 0) // q > 0.5
				{
					n += 1;
					int32_t carry = 0;
					for (int32_t i = 0; i < jz; i++) // compute 1-q
					{
						j = iq[i];
						if (carry == 0)
						{
							if (j != 0)
							{
								carry = 1;
								iq[i] = 0x1000000 - j;
							}
						}
						else
						{
							iq[i] = 0xffffff - j;
						}
					}
					if (q0 > 0) // rare case: chance is 1 in 12
					{
						switch (q0)
						{
							case 1:
								iq[jz - 1] &= 0x7fffff;
								break;
							case 2:
								iq[jz - 1] &= 0x3fffff;
								break;
							default:
								break;
						}
					}
					if (ih == 2)
					{
						z = 1.0 - z;
						if (carry != 0)
						{
							z -= ScaleByPowerOfTwo(1.0, q0);
						}
					}
				}

				// Check if recomputation is needed.
				if (z == 0.0)
				{
					j = 0;
					for (int32_t i = jz - 1; i >= jk; i--)
					{
						j |= iq[i];
					}
					if (j == 0) // need recomputation
					{
						int32_t k = 1; // number of terms needed
						while (iq[jk - k] == 0)
						{
							k++;
						}
						for (int32_t i = jz + 1; i <= jz + k; i++) // add q[jz+1] to q[jz+k]
						{
							f[jx + i] = static_cast<double>(TwoOverPi[jv + i]);
							double fw = 0.0;
							for (j = 0; j <= jx; j++)
							{
								fw += x[j] * f[jx + i - j];
							}
							q[i] = fw;
						}
						jz += k;
						continue;
					}
				}
				break;
			}

			// Chop off zero terms.
			if (z == 0.0)
			{
				jz -= 1;
				q0 -= 24;
				while (iq[jz] == 0)
				{
					jz--;
					q0 -= 24;
				}
			}
			else // break z into 24-bit pieces if necessary
			{
				z = ScaleByPowerOfTwo(z, -q0);
				if (z >= Two24)
				{
					const double fw = static_cast<double>(static_cast<int32_t>(TwoMinus24 * z));
					iq[jz] = static_cast<int32_t>(z - Two24 * fw);
					jz += 1;
					q0 += 24;
					iq[jz] = static_cast<int32_t>(fw);
				}
				else
				{
					iq[jz] = static_cast<int32_t>(z);
				}
			}

			// Convert the integer "bit" chunks to floating point values.
			double fw = ScaleByPowerOfTwo(1.0, q0);
			for (int32_t i = jz; i >= 0; i--)
			{
				q[i] = fw * static_cast<double>(iq[i]);
				fw *= TwoMinus24;
			}

			// Compute PiOver2Chunks[0..jp] * q[jz..0].
			for (int32_t i = jz; i >= 0; i--)
			{
				fw = 0.0;
				for (int32_t k = 0; k <= jp && k <= jz - i; k++)
				{
					fw += PiOver2Chunks[k] * q[i + k];
				}
				fq[jz - i] = fw;
			}

			// Compress fq[] into y[].
			fw = 0.0;
			for (int32_t i = jz; i >= 0; i--)
			{
				fw += fq[i];
			}
			y[0] = ih == 0 ? fw : -fw;
			fw = fq[0] - fw;
			for (int32_t i = 1; i <= jz; i++)
			{
				fw += fq[i];
			}
			y[1] = ih == 0 ? fw : -fw;
			return n & 7;
		}

		// ---- argument reduction (e_rem_pio2.c) ----

		// invpio2: 53 bits of 2/pi; PiOver2_1: first 33 bits of pi/2; PiOver2_1T: pi/2 - PiOver2_1; the _2 and _3
		// values are the following 33-bit chunks and their tails.
		constexpr double ToInt =
			6755399441055744.0; // 1.5 / DBL_EPSILON: rounds to nearest integer when added and subtracted
		constexpr double InvPiOver2 = 6.36619772367581382433e-01; // 0x3FE45F30, 0x6DC9C883
		constexpr double PiOver2_1 = 1.57079632673412561417e+00;  // 0x3FF921FB, 0x54400000
		constexpr double PiOver2_1T = 6.07710050650619224932e-11; // 0x3DD0B461, 0x1A626331
		constexpr double PiOver2_2 = 6.07710050630396597660e-11;  // 0x3DD0B461, 0x1A600000
		constexpr double PiOver2_2T = 2.02226624879595063154e-21; // 0x3BA3198A, 0x2E037073
		constexpr double PiOver2_3 = 2.02226624871116645580e-21;  // 0x3BA3198A, 0x2E000000
		constexpr double PiOver2_3T = 8.47842766036889956997e-32; // 0x397B839A, 0x252049C1

		// Returns n and y[0] + y[1] = x - n*pi/2 with |y| <= pi/4 (n mod 4 selects the quadrant). The caller handles
		// |x| <= pi/4 and non-finite x.
		int ReducePiOver2(double x, double* y)
		{
			const uint64_t bits = AsBits(x);
			const bool negative = (bits >> 63) != 0;
			const uint32_t ix = (bits >> 32) & 0x7fffffff;
			bool medium = false; // cancellation near multiples of pi/2: use the general path

			if (ix <= 0x400f6a7a) // |x| ~<= 5pi/4
			{
				if ((ix & 0xfffff) == 0x921fb) // |x| ~= pi/2 or 2pi/2
				{
					medium = true;
				}
				else if (ix <= 0x4002d97c) // |x| ~<= 3pi/4
				{
					if (!negative)
					{
						const double z = x - PiOver2_1; // one round good to 85 bits
						y[0] = z - PiOver2_1T;
						y[1] = (z - y[0]) - PiOver2_1T;
						return 1;
					}
					const double z = x + PiOver2_1;
					y[0] = z + PiOver2_1T;
					y[1] = (z - y[0]) + PiOver2_1T;
					return -1;
				}
				else
				{
					if (!negative)
					{
						const double z = x - 2 * PiOver2_1;
						y[0] = z - 2 * PiOver2_1T;
						y[1] = (z - y[0]) - 2 * PiOver2_1T;
						return 2;
					}
					const double z = x + 2 * PiOver2_1;
					y[0] = z + 2 * PiOver2_1T;
					y[1] = (z - y[0]) + 2 * PiOver2_1T;
					return -2;
				}
			}
			else if (ix <= 0x401c463b) // |x| ~<= 9pi/4
			{
				if (ix <= 0x4015fdbc) // |x| ~<= 7pi/4
				{
					if (ix == 0x4012d97c) // |x| ~= 3pi/2
					{
						medium = true;
					}
					else
					{
						if (!negative)
						{
							const double z = x - 3 * PiOver2_1;
							y[0] = z - 3 * PiOver2_1T;
							y[1] = (z - y[0]) - 3 * PiOver2_1T;
							return 3;
						}
						const double z = x + 3 * PiOver2_1;
						y[0] = z + 3 * PiOver2_1T;
						y[1] = (z - y[0]) + 3 * PiOver2_1T;
						return -3;
					}
				}
				else
				{
					if (ix == 0x401921fb) // |x| ~= 4pi/2
					{
						medium = true;
					}
					else
					{
						if (!negative)
						{
							const double z = x - 4 * PiOver2_1;
							y[0] = z - 4 * PiOver2_1T;
							y[1] = (z - y[0]) - 4 * PiOver2_1T;
							return 4;
						}
						const double z = x + 4 * PiOver2_1;
						y[0] = z + 4 * PiOver2_1T;
						y[1] = (z - y[0]) + 4 * PiOver2_1T;
						return -4;
					}
				}
			}

			if (medium || ix < 0x413921fb) // |x| ~< 2^20*(pi/2), medium size
			{
				// rint(x/(pi/2))
				double fn = x * InvPiOver2 + ToInt - ToInt;
				int n = static_cast<int32_t>(fn);
				double r = x - fn * PiOver2_1;
				double w = fn * PiOver2_1T; // 1st round, good to 85 bits
				// The checks below only matter with directed rounding.
				if (r - w < -Pio4)
				{
					n--;
					fn--;
					r = x - fn * PiOver2_1;
					w = fn * PiOver2_1T;
				}
				else if (r - w > Pio4)
				{
					n++;
					fn++;
					r = x - fn * PiOver2_1;
					w = fn * PiOver2_1T;
				}
				y[0] = r - w;
				int ey = static_cast<int>((AsBits(y[0]) >> 52) & 0x7ff);
				const int ex = static_cast<int>(ix >> 20);
				if (ex - ey > 16) // 2nd round, good to 118 bits
				{
					double t = r;
					w = fn * PiOver2_2;
					r = t - w;
					w = fn * PiOver2_2T - ((t - r) - w);
					y[0] = r - w;
					ey = static_cast<int>((AsBits(y[0]) >> 52) & 0x7ff);
					if (ex - ey > 49) // 3rd round, good to 151 bits, covers all cases
					{
						t = r;
						w = fn * PiOver2_3;
						r = t - w;
						w = fn * PiOver2_3T - ((t - r) - w);
						y[0] = r - w;
					}
				}
				y[1] = (r - y[0]) - w;
				return n;
			}

			// All other (large) arguments: x is finite here.
			// Set z = scalbn(|x|, -ilogb(x) + 23).
			uint64_t scaled = bits;
			scaled &= ~uint64_t{ 0 } >> 12;
			scaled |= static_cast<uint64_t>(0x3ff + 23) << 52;
			double z = FromBits(scaled);
			double tx[3];
			double ty[2];
			int i = 0;
			for (; i < 2; i++)
			{
				tx[i] = static_cast<double>(static_cast<int32_t>(z));
				z = (z - tx[i]) * Two24;
			}
			tx[i] = z;
			// Skip zero terms, the first term is non-zero.
			while (tx[i] == 0.0)
			{
				i--;
			}
			const int n = ReducePiOver2Large(tx, ty, static_cast<int>(ix >> 20) - (0x3ff + 23), i + 1);
			if (negative)
			{
				y[0] = -ty[0];
				y[1] = -ty[1];
				return -n;
			}
			y[0] = ty[0];
			y[1] = ty[1];
			return n;
		}
	}

	double Sin(double x)
	{
		const uint32_t ix = HighWord(x) & 0x7fffffff;

		// |x| ~< pi/4
		if (ix <= 0x3fe921fb)
		{
			if (ix < 0x3e500000) // |x| < 2**-26
			{
				return x;
			}
			return KernelSin(x, 0.0, false);
		}

		// sin(Inf or NaN) is NaN.
		if (ix >= 0x7ff00000)
		{
			return QuietNaN();
		}

		double y[2];
		const unsigned n = static_cast<unsigned>(ReducePiOver2(x, y));
		switch (n & 3)
		{
			case 0:
				return KernelSin(y[0], y[1], true);
			case 1:
				return KernelCos(y[0], y[1]);
			case 2:
				return -KernelSin(y[0], y[1], true);
			default:
				return -KernelCos(y[0], y[1]);
		}
	}

	double Cos(double x)
	{
		const uint32_t ix = HighWord(x) & 0x7fffffff;

		// |x| ~< pi/4
		if (ix <= 0x3fe921fb)
		{
			if (ix < 0x3e46a09e) // |x| < 2**-27 * sqrt(2)
			{
				return 1.0;
			}
			return KernelCos(x, 0);
		}

		// cos(Inf or NaN) is NaN.
		if (ix >= 0x7ff00000)
		{
			return QuietNaN();
		}

		double y[2];
		const unsigned n = static_cast<unsigned>(ReducePiOver2(x, y));
		switch (n & 3)
		{
			case 0:
				return KernelCos(y[0], y[1]);
			case 1:
				return -KernelSin(y[0], y[1], true);
			case 2:
				return -KernelCos(y[0], y[1]);
			default:
				return KernelSin(y[0], y[1], true);
		}
	}

	double Tan(double x)
	{
		const uint32_t ix = HighWord(x) & 0x7fffffff;

		// |x| ~< pi/4
		if (ix <= 0x3fe921fb)
		{
			if (ix < 0x3e400000) // |x| < 2**-27
			{
				return x;
			}
			return KernelTan(x, 0.0, false);
		}

		// tan(Inf or NaN) is NaN.
		if (ix >= 0x7ff00000)
		{
			return QuietNaN();
		}

		double y[2];
		const unsigned n = static_cast<unsigned>(ReducePiOver2(x, y));
		return KernelTan(y[0], y[1], (n & 1) != 0);
	}
}
