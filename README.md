# Gambit

Gambit is a production-grade, deterministic 3D game engine written in C++20. It renders with NVRHI on Vulkan,
simulates on a fixed timestep with bit-reproducible results, uses EnTT for a strict ECS, Jolt for physics and Lua for
game scripting. Everything an editor user can do, an agent can do: the editor, the `gambit` command-line tool and an
MCP server all drive the same command registry.

Status: bootstrap (version 0.1.0). The skeleton builds and is tested; engine features arrive task by task, see the
[roadmap](docs/ROADMAP.md).

## Build quickstart

Requirements: CMake 3.24+, Ninja, Python 3, and one of GCC 13+, Clang 16+, MSVC 2022 or AppleClang 15+.
`clang-format` 18 is needed for `scripts/format.py`; `ccache` is used automatically when installed.

```sh
git clone https://github.com/EncinoStudios/Gambit
cd Gambit
git submodule update --init --recursive --depth 1   # --depth 1: ~100 MB instead of ~700 MB of history

python3 scripts/check.py            # format check + build + all tests (Debug)
build/Debug/bin/gambit --version    # -> gambit 0.1.0
```

On Linux install `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev wayland-protocols
libxkbcommon-dev` first (GPU tests additionally need a Vulkan driver; Mesa's `mesa-vulkan-drivers` provides lavapipe
for headless machines). On Windows use a Visual Studio 2022 environment; macOS needs Xcode 15 (and MoltenVK once the
renderer lands).

The scripts are quiet: they print errors and one summary line, and write full logs to `build/logs/`.

```sh
python3 scripts/build.py  [--config Debug|Release|Dist] [--target GambitTests] [--jobs N]
python3 scripts/test.py   [--config Debug] [--filter "<doctest pattern>"] [--no-gpu]
python3 scripts/format.py [--check]
python3 scripts/check.py  [--config Debug] [--no-gpu]
```

Choose the compiler with `CC` / `CXX`, e.g. `CC=clang CXX=clang++ python3 scripts/check.py`. Plain CMake works too:
`cmake --preset linux-debug && cmake --build build/Debug` (presets exist for `linux`, `windows` and `macos` in
`debug`, `release` and `dist`).

## Repository layout

| Directory | Contents |
|---|---|
| `Gambit/` | engine static library (`Gambit/src/Gambit/<Module>/...`) |
| `GambitCLI/` | the `gambit` command-line tool |
| `GambitRuntime/` | the shipping player |
| `GambitEditor/` | the editor (never shipped, not built in `Dist`) |
| `GambitTests/` | doctest unit and integration tests |
| `ThirdParty/` | pinned git submodules and their CMake wiring ([versions and licenses](ThirdParty/README.md)) |
| `scripts/` | quiet build, test, format and check scripts |
| `docs/` | requirements, decisions, roadmap and task briefs |
| `.claude/skills/` | step-by-step recipes for agents |

## Documentation

- [AGENTS.md](AGENTS.md): the development contract for every contributor, human or AI
- [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md): what Gambit must do
- [docs/DECISIONS.md](docs/DECISIONS.md): design decisions, cited as `D-0xx`
- [docs/ROADMAP.md](docs/ROADMAP.md): plan and status; task briefs live in `docs/tasks/`
- [.claude/skills/build-and-test](.claude/skills/build-and-test/SKILL.md) and
  [.claude/skills/code-review](.claude/skills/code-review/SKILL.md)
