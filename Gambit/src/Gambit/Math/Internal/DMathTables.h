#pragma once

#include <cstdint>

// Lookup tables and polynomial coefficients of DMath's exp, log, log2 and pow (musl ports); the data is defined in
// DMathExpLogTables.cpp. Not part of the public API.

namespace Gambit::DMath::Internal
{
	inline constexpr int ExpTableBits = 7;
	inline constexpr int LogTableBits = 7;
	inline constexpr int Log2TableBits = 6;
	inline constexpr int PowLogTableBits = 7;

	struct ExpData
	{
		double InvLn2N;   // N / ln(2)
		double NegLn2HiN; // -ln(2) / N, high part
		double NegLn2LoN; // -ln(2) / N, low part
		double Shift;     // adding and subtracting it rounds to an integer
		double Poly[4];   // coefficients of the polynomial for exp(r) - 1 - r
		// 2^(k/N) ~= H[k] * (1 + T[k]) for k in [0, N): Tab[2k] = bits of T[k], Tab[2k+1] = bits of H[k] - (k << 52) /
		// N
		uint64_t Tab[2 << ExpTableBits];
	};

	struct LogData
	{
		double Ln2Hi;
		double Ln2Lo;
		double Poly[5];   // polynomial around the table point; the first coefficient is -0.5
		double Poly1[11]; // polynomial for inputs close to 1
		struct
		{
			double InvC;
			double LogC;
		} Tab[1 << LogTableBits];
		struct
		{
			double CHi;
			double CLo;
		} Tab2[1 << LogTableBits]; // c as a head and a tail, for computing z/c - 1 without fma
	};

	struct Log2Data
	{
		double InvLn2Hi;
		double InvLn2Lo;
		double Poly[6];
		double Poly1[10];
		struct
		{
			double InvC;
			double LogC;
		} Tab[1 << Log2TableBits];
		struct
		{
			double CHi;
			double CLo;
		} Tab2[1 << Log2TableBits];
	};

	// The extended-precision logarithm inside pow.
	struct PowLogData
	{
		double Ln2Hi;
		double Ln2Lo;
		double Poly[7];
		struct
		{
			double InvC;
			double LogC;
			double LogCTail;
		} Tab[1 << PowLogTableBits];
	};

	extern const ExpData ExpTables;
	extern const LogData LogTables;
	extern const Log2Data Log2Tables;
	extern const PowLogData PowLogTables;
}
