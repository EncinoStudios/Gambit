---
name: build-and-test
description: Build, test, format and debug build or test failures in the Gambit repository without flooding the context. Use whenever you need to compile the engine, run unit tests, check formatting, switch compilers or configurations, or investigate a failing build, test or CI job.
---

# Build and test Gambit (quietly)

The scripts in `scripts/` print only errors and one summary line (D-006). Full output is always in
`build/logs/`. Never run raw `cmake --build`, `ninja` or the test executable and paste their output; use the scripts
and read the logs selectively.

## The loop

```sh
git submodule update --init --recursive --depth 1   # first time or after a submodule bump (--depth 1 skips history)
python3 scripts/build.py                     # configure if needed, build; Debug by default
python3 scripts/test.py                      # all tests
python3 scripts/format.py                    # fix formatting (never touches ThirdParty/)
python3 scripts/check.py                     # format check + build + tests: required before every commit
```

Useful options: `--config Debug|Release|Dist`, `--target GambitTests` (build one target), `--jobs N` (the build
defaults to all cores; use `--jobs 4` or `GB_JOBS=4` on small machines), `--filter "Log: *"` and `--no-gpu` (tests),
`--check` (format only verifies), `--skip-format` (`check.py`, for CI jobs without clang-format 18).

Choose the compiler with environment variables; the script recreates the build directory when it changes:

```sh
CC=clang CXX=clang++ python3 scripts/check.py
```

Both GCC and Clang must stay warning-free (`GB_WARNINGS_AS_ERRORS` is ON), in Debug, Release and Dist.

## Summary lines

```
build: OK Debug (GNU 13.3.0) in 14.7s
test: OK Debug - 60 test cases, 6321 assertions, 5 smoke tests in 0.5s
format: OK (27 files) in 0.1s
check: OK (format, build, test) Debug in 40.2s
```

A failure prints the relevant excerpt plus the log path. `check.py` stops at the first failing step.

## Debugging failures

| Symptom | What to do |
|---|---|
| `build: FAILED` | The excerpt starts at the first `FAILED:` step. Fix the first error only; later ones are usually fallout. For more context: `grep -n "error" build/logs/build-Debug.log \| head`. |
| `FAILED to configure` | Read `build/logs/configure-<config>.log`. "ThirdParty/x is missing" means run `git submodule update --init --recursive`. |
| A warning became an error | Fix the code. Do not disable warnings; third-party headers are already `SYSTEM` includes. |
| `test: FAILED` | The excerpt shows the doctest failure with file and line. Re-run one case: `python3 scripts/test.py --filter "Result: *"`. The full log is `build/logs/test-<config>.log`. |
| Test crashed (no doctest summary) | Run the binary directly under a debugger: `build/Debug/bin/GambitTests --test-case="<name>"`. An assertion trap (`SIGTRAP`) means a `GB_ASSERT` / `GB_VERIFY` fired; its message is the last critical line in the log. |
| `format: FAILED` | Run `python3 scripts/format.py`, review the diff, re-run the check. If your clang-format is not version 18 the result can differ; use 18. |
| Stale or odd build state | `python3 scripts/build.py --clean`. |
| Need compile commands for clangd | `build.py` links `compile_commands.json` at the repo root. |

## Other notes

- Build directories: `build/<Config>`; binaries in `build/<Config>/bin` (`gambit`, `GambitTests`, ...).
- The Dist configuration compiles out `GB_ASSERT` and skips the editor; run `python3 scripts/check.py --config Dist`
  after touching asserts or editor-only code.
- Lua project tests (later tasks): `build/<config>/bin/gambit test --project Projects/FeatureTest`.
- ccache is used automatically when installed (`-DGB_USE_CCACHE=OFF` to disable).
- Never commit `build/`, and never edit files under `ThirdParty/`.
