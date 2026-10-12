# T01 review

Verdict: **APPROVE-WITH-FIXES**. All brief deliverables exist and work. Pinned refs were verified against upstream
tags and the submodules are clean. `.clang-format` encodes AGENTS.md §2, and both skills are accurate. The
determinism and warning flags apply to first-party targets only (third-party targets get `-w` / `/w`).

## Fixed during review

1. **ccache served stale precompiled headers** (`cmake/GambitCompilerOptions.cmake`). With `pch_defines` sloppiness,
   ccache caches PCH creation in preprocessor mode, which cannot see `#define` changes. A header edit that only
   touched macros therefore mapped to the old `.gch`, and that result was then written into the direct-mode manifest.
   Reproduced as a failed GCC Debug build (GCC → Clang → GCC on `build/Debug`). It could also silently compile stale
   macro bodies. Now only `time_macros` is set: ccache declines to cache PCH creation and caches everything else.
2. **CMP0141 was never enabled** (`CMakeLists.txt`). `CMAKE_POLICY_DEFAULT_CMP0141` is read only by
   `cmake_minimum_required`, which had already run, so MSVC kept `/Zi` (which ccache cannot cache) instead of `/Z7`.
   Now `cmake_policy(SET CMP0141 NEW)` is used, verified with `cmake_policy(GET)`.
3. **LZ4's private xxhash clashed with xxHash** (`ThirdParty/CMakeLists.txt`). `XXH_NAMESPACE=LZ4_` was missing, so
   both archives exported `XXH32`/`XXH64`. `lz4/lib` (with an old `xxhash.h`) was also a public include directory, so
   `<xxhash.h>` resolved by link order. Now the symbols are namespaced and only LZ4's public headers are exposed (copied
   to the build tree). Regression test: an LZ4 frame round trip with checksum and corruption check. `GambitTests` now
   links lz4 before xxhash on purpose; without the fix this no longer compiles (`XXH3_64bits` undeclared).
4. **MSVC C4127 in assert macros** (`Assert.h`). `GB_CORE_ASSERT(false, ...)` and constant checks in the tests would
   fail `/W4 /WX`. The check is now wrapped in a `__pragma(warning(disable : 4127))` push/pop on MSVC only. The same
   risk in `LogTests.cpp` (`const bool flag = false; if (flag)`) was removed.
5. **`ConsoleColor::Always` was not "always" on Windows** (`LogSinks.cpp`). It returned whether `SetConsoleMode`
   succeeded, which fails on pipes and files. The console-sink test captures stderr into a file, so it would fail on
   Windows CI.
6. CI Windows job: `CC=cl` / `CXX=cl`. With Ninja, CMake takes the first `c++`/`g++`/`cl` on PATH, and the runner
   image also ships MinGW and Strawberry Perl.
7. Small fixes: `gambit` with `argc == 0` (out-of-range iterator), explicit casts for byte flips in tests (possible
   MSVC C4244), a stale comment in `GambitConfigurations.cmake`, and a stale AGENTS.md remark in `T01-notes.md`.

## Open (need a decision)

- **ccache in CI and PCH.** GCC `.gch` output is not byte-reproducible. On fresh build directories (every CI job),
  translation units that use the engine PCH therefore miss the cache; other units hit. As the engine grows, consider
  configuring CI with `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON` (this needs `check.py` to forward `-D`).
- **D-005 says GPU tests are tagged `[gpu]`.** The code and AGENTS.md use doctest suite `gpu`; D-005's text should
  be aligned.
- **Multi-config generators** (Visual Studio, Ninja Multi-Config) still build GambitEditor in Dist (the exclusion
  tests `CMAKE_BUILD_TYPE`). Presets and scripts use single-config Ninja, so this only affects IDE users.
- **Not verified here:** MSVC, AppleClang and the workflow itself. The code paths were reviewed by reading only.

## Result

`python3 scripts/check.py` passes for GCC 13.3 and Clang 18.1 in Debug, Release and Dist (61 test cases, zero
warnings). Option combinations (`GB_BUILD_CLI/EDITOR/RUNTIME/TESTS=OFF`), Ninja Multi-Config Dist and an
ASan+UBSan run of `GambitTests` also pass.
