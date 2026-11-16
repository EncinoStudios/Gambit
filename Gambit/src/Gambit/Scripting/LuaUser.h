#pragma once

/* Gambit's configuration of the Lua VM (D-041). lua.h includes this header through LUA_USER_H in every translation unit
   that uses Lua, the Lua library itself included, so all of them agree. It is plain C: the Lua library is compiled as
   C. Each setting removes a source of non-determinism from the VM (D-011). */

#ifdef __cplusplus
extern "C"
{
#endif

	/* The float exponentiation of the `^` operator. The platform's pow differs in the last bit between C libraries, so
	   the engine implements this with DMath::Pow (Scripting/Bindings/MathLibrary.cpp). */
	double gb_lua_pow(double base, double exponent);

#ifdef __cplusplus
}
#endif

/* The VM calls luai_numpow for every float `^`. */
#define luai_numpow(L, a, b) ((void)(L), gb_lua_pow((a), (b)))

/* The seed of the string hash: fixed instead of derived from the clock and from addresses, so that the order in which
   `next` and `pairs` visit string keys is the same in every run of the same build. */
#define luai_makeseed(L) ((void)(L), 0x47616D62u)

/* table.sort randomizes its pivots from the clock when a partition of a large array is badly unbalanced; a constant
   keeps the order of elements that compare equal (the sort is not stable) the same in every run. */
#define l_randomizePivot() 0x9E3779B9u
