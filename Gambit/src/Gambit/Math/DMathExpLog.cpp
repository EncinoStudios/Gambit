// Deterministic exp, log, log2 and pow (D-011).
//
// Port of musl libc 1.2.6 src/math/{exp,log,log2,pow}.c, which are Szabolcs Nagy's implementations from ARM's
// Optimized Routines (https://github.com/ARM-software/optimized-routines).
//
// The code uses IEEE-754 arithmetic (+ - * / and the exactly rounded sqrt, floor and fabs), integer operations and
// bit casts only: no call into the platform libm, no fenv side effects and no fused multiply-add (the build compiles
// with -ffp-contract=off, and the FMA variants of the original were removed). Changes against the original: C++ port
// with Gambit naming, std::bit_cast instead of unions, canonical NaN results (Internal/DMathBits.h), and no
// rounding-mode, exception-flag or long double code paths.
//
// ---- Notices of the original source files ----
//
// Copyright (c) 2018, Arm Limited.
// SPDX-License-Identifier: MIT
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
#include "Gambit/Math/Internal/DMathTables.h"

#include <cmath>

namespace Gambit::DMath
{
	using namespace Internal;

	namespace
	{
		constexpr double TwoPow1009 = PowerOfTwo(1009);
		constexpr double TwoPowMinus1022 = PowerOfTwo(-1022);

		// Top 12 bits of a double (sign and exponent), and the exponents the fast paths are bounded by.
		uint32_t Top12(double x) noexcept
		{
			return static_cast<uint32_t>(AsBits(x) >> 52);
		}

		constexpr uint32_t Top12OfTwoPowMinus54 = 0x3c9; // 0x1p-54
		constexpr uint32_t Top12Of512 = 0x408;
		constexpr uint32_t Top12Of1024 = 0x409;
		constexpr uint32_t Top12OfInfinity = 0x7ff;

		// Top 16 bits of a double.
		uint32_t Top16(double x) noexcept
		{
			return static_cast<uint32_t>(AsBits(x) >> 48);
		}

		// Results that over- or underflow, with the sign they carry.
		double Overflow(bool negative) noexcept
		{
			return negative ? -PositiveInfinity() : PositiveInfinity();
		}

		double Underflow(bool negative) noexcept
		{
			return negative ? -0.0 : 0.0;
		}

		// ---- exp (exp.c, shared with pow) ----

		// Handles the cases that may overflow or underflow when computing scale * (1 + tmp) without intermediate
		// rounding. sbits holds the bits of scale, whose computed exponent may have overflowed into the sign bit, so
		// that has to be adjusted first. (int32_t)ki is the k of the argument reduction: positive k means the result
		// may overflow and negative k that it may underflow.
		double ExpSpecialCase(double tmp, uint64_t sbits, uint64_t ki)
		{
			if ((ki & 0x80000000) == 0)
			{
				// k > 0, the exponent of scale might have overflowed by <= 460.
				sbits -= 1009ull << 52;
				const double scale = FromBits(sbits);
				return TwoPow1009 * (scale + scale * tmp);
			}
			// k < 0, need special care in the subnormal range.
			sbits += 1022ull << 52;
			// Note: sbits is the signed scale.
			const double scale = FromBits(sbits);
			double y = scale + scale * tmp;
			if (std::fabs(y) < 1.0)
			{
				// Round y to the right precision before scaling it into the subnormal range to avoid double rounding
				// that can cause 0.5+E/2 ulp error where E is the worst-case ulp error outside the subnormal range.
				double one = 1.0;
				if (y < 0.0)
				{
					one = -1.0;
				}
				double lo = scale - y + scale * tmp;
				const double hi = one + y;
				lo = one - hi + y + lo;
				y = (hi + lo) - one;
				// Fix the sign of 0.
				if (y == 0.0)
				{
					y = FromBits(sbits & 0x8000000000000000ull);
				}
			}
			return TwoPowMinus1022 * y;
		}

		constexpr uint32_t PowSignBias = 0x800u << ExpTableBits;

		// Computes sign * exp(x + xtail) where |xtail| < 2^-8/N and |xtail| <= |x|. signBias is PowSignBias or 0 and
		// sets the sign to -1 or 1.
		double ExpInline(double x, double xtail, uint32_t signBias)
		{
			const ExpData& data = ExpTables;
			constexpr uint64_t N = 1u << ExpTableBits;
			uint32_t abstop = Top12(x) & 0x7ff;
			if (abstop - Top12OfTwoPowMinus54 >= Top12Of512 - Top12OfTwoPowMinus54)
			{
				if (abstop - Top12OfTwoPowMinus54 >= 0x80000000)
				{
					// Avoid spurious underflow for tiny x. Note: 0 is common input.
					const double one = 1.0 + x;
					return signBias != 0 ? -one : one;
				}
				if (abstop >= Top12Of1024)
				{
					// Note: inf and nan are already handled.
					if ((AsBits(x) >> 63) != 0)
					{
						return Underflow(signBias != 0);
					}
					return Overflow(signBias != 0);
				}
				// Large x is special cased below.
				abstop = 0;
			}

			// exp(x) = 2^(k/N) * exp(r), with exp(r) in [2^(-1/2N), 2^(1/2N)].
			// x = ln2/N*k + r, with int k and r in [-ln2/2N, ln2/2N].
			const double z = data.InvLn2N * x;
			// z - kd is in [-1, 1] in non-nearest rounding modes.
			double kd = z + data.Shift;
			const uint64_t ki = AsBits(kd);
			kd -= data.Shift;
			double r = x + kd * data.NegLn2HiN + kd * data.NegLn2LoN;
			// The code assumes 2^-200 < |xtail| < 2^-8/N.
			r += xtail;
			// 2^(k/N) ~= scale * (1 + tail).
			const uint64_t idx = 2 * (ki % N);
			const uint64_t top = (ki + signBias) << (52 - ExpTableBits);
			const double tail = FromBits(data.Tab[idx]);
			// This is only a valid scale when -1023*N < k < 1024*N.
			const uint64_t sbits = data.Tab[idx + 1] + top;
			// exp(x) = 2^(k/N) * exp(r) ~= scale + scale * (tail + exp(r) - 1).
			const double r2 = r * r;
			const double tmp =
				tail + r + r2 * (data.Poly[0] + r * data.Poly[1]) + r2 * r2 * (data.Poly[2] + r * data.Poly[3]);
			if (abstop == 0)
			{
				return ExpSpecialCase(tmp, sbits, ki);
			}
			const double scale = FromBits(sbits);
			// Note: tmp == 0 or |tmp| > 2^-200 and scale > 2^-739, so there is no spurious underflow here.
			return scale + scale * tmp;
		}

		// ---- pow (pow.c) ----

		// log(x) as y + tail, where y is the rounded result and tail has about 15 more bits of precision. ix holds the
		// bits of x, normalized in the subnormal range using the sign bit for the exponent.
		double PowLogInline(uint64_t ix, double& tail)
		{
			const PowLogData& data = PowLogTables;
			constexpr uint64_t N = 1u << PowLogTableBits;
			constexpr uint64_t Offset = 0x3fe6955500000000ull;

			// x = 2^k z; where z is in range [Offset, 2*Offset) and exact.
			// The range is split into N subintervals.
			// The ith subinterval contains z and c is near its center.
			const uint64_t tmp = ix - Offset;
			const uint64_t i = (tmp >> (52 - PowLogTableBits)) % N;
			const int k = static_cast<int>(static_cast<int64_t>(tmp) >> 52); // arithmetic shift
			const uint64_t iz = ix - (tmp & (0xfffull << 52));
			const double z = FromBits(iz);
			const double kd = static_cast<double>(k);

			// log(x) = k*Ln2 + log(c) + log1p(z/c-1).
			const double invc = data.Tab[i].InvC;
			const double logc = data.Tab[i].LogC;
			const double logctail = data.Tab[i].LogCTail;

			// Note: 1/c is j/N or j/N/2 where j is an integer in [N,2N) and |z/c - 1| < 1/N, so r = z/c - 1 is exactly
			// representable. Split z such that rhi, rlo and rhi*rhi are exact and |rlo| <= |r|.
			const double zhi = FromBits((iz + (1ull << 31)) & (~0ull << 32));
			const double zlo = z - zhi;
			const double rhi = zhi * invc - 1.0;
			const double rlo = zlo * invc;
			const double r = rhi + rlo;

			// k*Ln2 + log(c) + r.
			const double t1 = kd * data.Ln2Hi + logc;
			const double t2 = t1 + r;
			const double lo1 = kd * data.Ln2Lo + logctail;
			const double lo2 = t1 - t2 + r;

			// Evaluation is optimized assuming superscalar pipelined execution.
			const double* const A = data.Poly;
			const double ar = A[0] * r; // A[0] = -0.5
			const double ar2 = r * ar;
			const double ar3 = r * ar2;
			// k*Ln2 + log(c) + r + A[0]*r*r.
			const double arhi = A[0] * rhi;
			const double arhi2 = rhi * arhi;
			const double hi = t2 + arhi2;
			const double lo3 = rlo * (ar + arhi);
			const double lo4 = t2 - hi + arhi2;
			// p = log1p(r) - r - A[0]*r*r.
			const double p = ar3 * (A[1] + r * A[2] + ar2 * (A[3] + r * A[4] + ar2 * (A[5] + r * A[6])));
			const double lo = lo1 + lo2 + lo3 + lo4 + p;
			const double y = hi + lo;
			tail = hi - y + lo;
			return y;
		}

		// Returns 0 if the value is not an integer, 1 if it is odd and 2 if it is even. The argument is the bit
		// representation of a non-zero finite floating-point value.
		int CheckInteger(uint64_t iy) noexcept
		{
			const int e = static_cast<int>((iy >> 52) & 0x7ff);
			if (e < 0x3ff)
			{
				return 0;
			}
			if (e > 0x3ff + 52)
			{
				return 2;
			}
			if ((iy & ((1ull << (0x3ff + 52 - e)) - 1)) != 0)
			{
				return 0;
			}
			if ((iy & (1ull << (0x3ff + 52 - e))) != 0)
			{
				return 1;
			}
			return 2;
		}

		// True if the bit representation is that of 0, infinity or NaN.
		bool IsZeroInfNaN(uint64_t i) noexcept
		{
			return 2 * i - 1 >= 2 * AsBits(PositiveInfinity()) - 1;
		}
	}

	double Exp(double x)
	{
		const ExpData& data = ExpTables;
		constexpr uint64_t N = 1u << ExpTableBits;
		uint32_t abstop = Top12(x) & 0x7ff;
		if (abstop - Top12OfTwoPowMinus54 >= Top12Of512 - Top12OfTwoPowMinus54)
		{
			if (abstop - Top12OfTwoPowMinus54 >= 0x80000000)
			{
				// Avoid spurious underflow for tiny x. Note: 0 is common input.
				return 1.0 + x;
			}
			if (abstop >= Top12Of1024)
			{
				if (AsBits(x) == AsBits(-PositiveInfinity()))
				{
					return 0.0;
				}
				if (abstop >= Top12OfInfinity)
				{
					return x != x ? QuietNaN() : x; // exp(+inf) = +inf
				}
				if ((AsBits(x) >> 63) != 0)
				{
					return Underflow(false);
				}
				return Overflow(false);
			}
			// Large x is special cased below.
			abstop = 0;
		}

		// exp(x) = 2^(k/N) * exp(r), with exp(r) in [2^(-1/2N), 2^(1/2N)].
		// x = ln2/N*k + r, with int k and r in [-ln2/2N, ln2/2N].
		const double z = data.InvLn2N * x;
		// z - kd is in [-1, 1] in non-nearest rounding modes.
		double kd = z + data.Shift;
		const uint64_t ki = AsBits(kd);
		kd -= data.Shift;
		const double r = x + kd * data.NegLn2HiN + kd * data.NegLn2LoN;
		// 2^(k/N) ~= scale * (1 + tail).
		const uint64_t idx = 2 * (ki % N);
		const uint64_t top = ki << (52 - ExpTableBits);
		const double tail = FromBits(data.Tab[idx]);
		// This is only a valid scale when -1023*N < k < 1024*N.
		const uint64_t sbits = data.Tab[idx + 1] + top;
		// exp(x) = 2^(k/N) * exp(r) ~= scale + scale * (tail + exp(r) - 1).
		const double r2 = r * r;
		const double tmp =
			tail + r + r2 * (data.Poly[0] + r * data.Poly[1]) + r2 * r2 * (data.Poly[2] + r * data.Poly[3]);
		if (abstop == 0)
		{
			return ExpSpecialCase(tmp, sbits, ki);
		}
		const double scale = FromBits(sbits);
		// Note: tmp == 0 or |tmp| > 2^-200 and scale > 2^-739, so there is no spurious underflow here.
		return scale + scale * tmp;
	}

	double Log(double x)
	{
		const LogData& data = LogTables;
		constexpr uint64_t N = 1u << LogTableBits;
		constexpr uint64_t Offset = 0x3fe6000000000000ull;
		constexpr uint64_t NearOneLow = 0x3fee000000000000ull;  // bits of 1.0 - 0x1p-4
		constexpr uint64_t NearOneHigh = 0x3ff1090000000000ull; // bits of 1.0 + 0x1.09p-4

		uint64_t ix = AsBits(x);
		const uint32_t top = Top16(x);
		if (ix - NearOneLow < NearOneHigh - NearOneLow)
		{
			// Handle close to 1.0 inputs separately.
			if (ix == AsBits(1.0))
			{
				return 0;
			}
			const double* const B = data.Poly1;
			const double r = x - 1.0;
			const double r2 = r * r;
			const double r3 = r * r2;
			double y = r3
				* (B[1] + r * B[2] + r2 * B[3]
			       + r3 * (B[4] + r * B[5] + r2 * B[6] + r3 * (B[7] + r * B[8] + r2 * B[9] + r3 * B[10])));
			// Worst-case error is around 0.507 ULP.
			double w = r * 134217728.0; // 0x1p27
			const double rhi = r + w - w;
			const double rlo = r - rhi;
			w = rhi * rhi * B[0]; // B[0] == -0.5
			const double hi = r + w;
			double lo = r - hi + w;
			lo += B[0] * rlo * (rhi + r);
			y += lo;
			y += hi;
			return y;
		}
		if (top - 0x0010 >= 0x7ff0 - 0x0010)
		{
			// x < 0x1p-1022 or inf or nan.
			if (ix * 2 == 0)
			{
				return -PositiveInfinity(); // log(+-0)
			}
			if (ix == AsBits(PositiveInfinity()))
			{
				return x; // log(inf) == inf
			}
			if ((top & 0x8000) != 0 || (top & 0x7ff0) == 0x7ff0)
			{
				return QuietNaN(); // negative or NaN
			}
			// x is subnormal, normalize it.
			ix = AsBits(x * 4503599627370496.0); // 0x1p52
			ix -= 52ull << 52;
		}

		// x = 2^k z; where z is in range [Offset, 2*Offset) and exact.
		// The range is split into N subintervals.
		// The ith subinterval contains z and c is near its center.
		const uint64_t tmp = ix - Offset;
		const uint64_t i = (tmp >> (52 - LogTableBits)) % N;
		const int k = static_cast<int>(static_cast<int64_t>(tmp) >> 52); // arithmetic shift
		const uint64_t iz = ix - (tmp & (0xfffull << 52));
		const double invc = data.Tab[i].InvC;
		const double logc = data.Tab[i].LogC;
		const double z = FromBits(iz);

		// log(x) = log1p(z/c-1) + log(c) + k*Ln2.
		// r ~= z/c - 1, |r| < 1/(2*N). Rounding error: 0x1p-55/N + 0x1p-66.
		const double r = (z - data.Tab2[i].CHi - data.Tab2[i].CLo) * invc;
		const double kd = static_cast<double>(k);

		// hi + lo = r + log(c) + k*Ln2.
		const double w = kd * data.Ln2Hi + logc;
		const double hi = w + r;
		const double lo = w - hi + r + kd * data.Ln2Lo;

		// log(x) = lo + (log1p(r) - r) + hi.
		const double r2 = r * r; // rounding error: 0x1p-54/N^2
		const double* const A = data.Poly;
		return lo + r2 * A[0] + r * r2 * (A[1] + r * A[2] + r2 * (A[3] + r * A[4])) + hi;
	}

	double Log2(double x)
	{
		const Log2Data& data = Log2Tables;
		constexpr uint64_t N = 1u << Log2TableBits;
		constexpr uint64_t Offset = 0x3fe6000000000000ull;
		constexpr uint64_t NearOneLow = 0x3feea4af00000000ull;  // bits of 1.0 - 0x1.5b51p-5
		constexpr uint64_t NearOneHigh = 0x3ff0b55900000000ull; // bits of 1.0 + 0x1.6ab2p-5

		uint64_t ix = AsBits(x);
		const uint32_t top = Top16(x);
		if (ix - NearOneLow < NearOneHigh - NearOneLow)
		{
			// Handle close to 1.0 inputs separately.
			if (ix == AsBits(1.0))
			{
				return 0;
			}
			const double* const B = data.Poly1;
			const double r = x - 1.0;
			const double rhi = FromBits(AsBits(r) & (~0ull << 32));
			const double rlo = r - rhi;
			const double hi = rhi * data.InvLn2Hi;
			double lo = rlo * data.InvLn2Hi + r * data.InvLn2Lo;
			const double r2 = r * r; // rounding error: 0x1p-62
			const double r4 = r2 * r2;
			// Worst-case error is less than 0.55 ULP.
			const double p = r2 * (B[0] + r * B[1]);
			double y = hi + p;
			lo += hi - y + p;
			lo += r4 * (B[2] + r * B[3] + r2 * (B[4] + r * B[5]) + r4 * (B[6] + r * B[7] + r2 * (B[8] + r * B[9])));
			y += lo;
			return y;
		}
		if (top - 0x0010 >= 0x7ff0 - 0x0010)
		{
			// x < 0x1p-1022 or inf or nan.
			if (ix * 2 == 0)
			{
				return -PositiveInfinity(); // log2(+-0)
			}
			if (ix == AsBits(PositiveInfinity()))
			{
				return x; // log2(inf) == inf
			}
			if ((top & 0x8000) != 0 || (top & 0x7ff0) == 0x7ff0)
			{
				return QuietNaN(); // negative or NaN
			}
			// x is subnormal, normalize it.
			ix = AsBits(x * 4503599627370496.0); // 0x1p52
			ix -= 52ull << 52;
		}

		// x = 2^k z; where z is in range [Offset, 2*Offset) and exact.
		// The range is split into N subintervals.
		// The ith subinterval contains z and c is near its center.
		const uint64_t tmp = ix - Offset;
		const uint64_t i = (tmp >> (52 - Log2TableBits)) % N;
		const int k = static_cast<int>(static_cast<int64_t>(tmp) >> 52); // arithmetic shift
		const uint64_t iz = ix - (tmp & (0xfffull << 52));
		const double invc = data.Tab[i].InvC;
		const double logc = data.Tab[i].LogC;
		const double z = FromBits(iz);
		const double kd = static_cast<double>(k);

		// log2(x) = log2(z/c) + log2(c) + k.
		// r ~= z/c - 1, |r| < 1/(2*N). Rounding error: 0x1p-55/N + 0x1p-65.
		const double r = (z - data.Tab2[i].CHi - data.Tab2[i].CLo) * invc;
		const double rhi = FromBits(AsBits(r) & (~0ull << 32));
		const double rlo = r - rhi;
		const double t1 = rhi * data.InvLn2Hi;
		const double t2 = rlo * data.InvLn2Hi + r * data.InvLn2Lo;

		// hi + lo = r/ln2 + log2(c) + k.
		const double t3 = kd + logc;
		const double hi = t3 + t1;
		const double lo = t3 - hi + t1 + t2;

		// log2(r+1) = r/ln2 + r^2*poly(r).
		const double r2 = r * r; // rounding error: 0x1p-54/N^2
		const double r4 = r2 * r2;
		const double* const A = data.Poly;
		const double p = A[0] + r * A[1] + r2 * (A[2] + r * A[3]) + r4 * (A[4] + r * A[5]);
		return lo + r2 * p + hi;
	}

	double Pow(double x, double y)
	{
		uint32_t signBias = 0;
		uint64_t ix = AsBits(x);
		const uint64_t iy = AsBits(y);
		uint32_t topx = Top12(x);
		const uint32_t topy = Top12(y);
		if (topx - 0x001 >= 0x7ff - 0x001 || (topy & 0x7ff) - 0x3be >= 0x43e - 0x3be)
		{
			// Note: if |y| > 1075 * ln2 * 2^53 ~= 0x1.749p62 then pow(x,y) = inf/0 and if
			// |y| < 2^-54 / 1075 ~= 0x1.e7b6p-65 then pow(x,y) = +-1.
			// Special cases: (x < 0x1p-126 or inf or nan) or (|y| < 0x1p-65 or |y| >= 0x1p63 or nan).
			if (IsZeroInfNaN(iy))
			{
				if (2 * iy == 0)
				{
					return 1.0;
				}
				if (ix == AsBits(1.0))
				{
					return 1.0;
				}
				if (2 * ix > 2 * AsBits(PositiveInfinity()) || 2 * iy > 2 * AsBits(PositiveInfinity()))
				{
					return QuietNaN();
				}
				if (2 * ix == 2 * AsBits(1.0))
				{
					return 1.0;
				}
				if ((2 * ix < 2 * AsBits(1.0)) == !(iy >> 63))
				{
					return 0.0; // |x|<1 && y==inf or |x|>1 && y==-inf
				}
				return y * y;
			}
			if (IsZeroInfNaN(ix))
			{
				if (x != x)
				{
					return QuietNaN();
				}
				double x2 = x * x;
				if ((ix >> 63) != 0 && CheckInteger(iy) == 1)
				{
					x2 = -x2;
				}
				return (iy >> 63) != 0 ? 1 / x2 : x2;
			}
			// Here x and y are non-zero finite.
			if ((ix >> 63) != 0)
			{
				// Finite x < 0.
				const int yint = CheckInteger(iy);
				if (yint == 0)
				{
					return QuietNaN();
				}
				if (yint == 1)
				{
					signBias = PowSignBias;
				}
				ix &= 0x7fffffffffffffffull;
				topx &= 0x7ff;
			}
			if ((topy & 0x7ff) - 0x3be >= 0x43e - 0x3be)
			{
				// Note: signBias == 0 here because y is not odd.
				if (ix == AsBits(1.0))
				{
					return 1.0;
				}
				if ((topy & 0x7ff) < 0x3be)
				{
					// |y| < 2^-65, x^y ~= 1 + y*log(x).
					return ix > AsBits(1.0) ? 1.0 + y : 1.0 - y;
				}
				return (ix > AsBits(1.0)) == (topy < 0x800) ? Overflow(false) : Underflow(false);
			}
			if (topx == 0)
			{
				// Normalize subnormal x so exponent becomes negative.
				ix = AsBits(x * 4503599627370496.0); // 0x1p52
				ix &= 0x7fffffffffffffffull;
				ix -= 52ull << 52;
			}
		}

		double lo = 0.0;
		const double hi = PowLogInline(ix, lo);
		const double yhi = FromBits(iy & (~0ull << 27));
		const double ylo = y - yhi;
		const double lhi = FromBits(AsBits(hi) & (~0ull << 27));
		const double llo = hi - lhi + lo;
		const double ehi = yhi * lhi;
		const double elo = ylo * lhi + y * llo; // |elo| < |ehi| * 2^-25
		return ExpInline(ehi, elo, signBias);
	}
}
