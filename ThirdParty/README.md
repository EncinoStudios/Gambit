# ThirdParty

Every dependency is a pinned git submodule (D-003). Rules:

- Never edit anything inside a submodule. Configuration and patches live in `ThirdParty/CMakeLists.txt` or in
  engine-side wrappers.
- Submodules are shallow (`shallow = true` in `.gitmodules`); the pinned commit is recorded in the superproject.
- Licenses must be permissive (D-003). Adding a dependency needs a `docs/DECISIONS.md` entry.
- `git submodule update --init --recursive` fetches everything. NVRHI currently has no nested submodules.

## Pinned versions

| Dependency | Path | Pinned ref | Commit | License | Status |
|---|---|---|---|---|---|
| GLFW | `glfw` | tag `3.4` | `7b6aead9fb88` | zlib/libpng | wired: `glfw::glfw` |
| NVRHI | `nvrhi` | `main` HEAD | `6b96fb03e075` | MIT | T07 |
| Vulkan-Headers | `Vulkan-Headers` | tag `v1.4.352` | `015e25c3c91b` | Apache-2.0 / MIT | T07 (see note) |
| glm | `glm` | tag `1.0.3` | `8d1fd52e5ab5` | MIT | wired: `glm::glm` |
| Dear ImGui | `imgui` | branch `docking` HEAD | `778992face67` | MIT | T20 |
| ImGuizmo | `ImGuizmo` | `master` HEAD | `105e4fde45e4` | MIT | T20 |
| EnTT | `entt` | tag `v3.16.0` | `b4e58bdd364a` | MIT | wired: `EnTT::EnTT` |
| JoltPhysics | `JoltPhysics` | tag `v5.6.0` | `e77f175595e6` | MIT | T13 |
| Lua | `lua` | tag `v5.4.8` | `6e22fedb74cf` | MIT | T11 |
| sol2 | `sol2` | tag `v3.5.0` | `9190880c593d` | MIT | T11 |
| miniaudio | `miniaudio` | tag `0.11.25` | `9634bedb5b5a` | public domain / MIT-0 | T16 |
| stb | `stb` | `master` HEAD | `2c980bb59875` | public domain / MIT | wired: `stb::stb` (headers only) |
| cgltf | `cgltf` | tag `v1.15` | `360db1a95480` | MIT | T08 |
| nlohmann/json | `nlohmann_json` | tag `v3.12.0` | `55f93686c015` | MIT | wired: `nlohmann_json::nlohmann_json` |
| doctest | `doctest` | tag `v2.5.3` | `2d0a9359a60c` | MIT | wired: `doctest::doctest` |
| glslang | `glslang` | tag `vulkan-sdk-1.4.363.0` | `e1b562a8bed2` | BSD-3-Clause (+ Apache-2.0, MIT parts; see `LICENSE.txt`) | T07 (see note) |
| nanosvg | `nanosvg` | `master` HEAD | `239e102ec2c6` | zlib | T18 |
| earcut.hpp | `earcut.hpp` | tag `v3.2.4` | `92acda9e2ed9` | ISC | T18 |
| cpp-httplib | `cpp-httplib` | tag `v0.60.1` | `00afaaed6aab` | MIT | T24 |
| Monocypher | `Monocypher` | tag `4.0.3` | `ab2b16dd619a` | BSD-2-Clause or CC0 | wired: `monocypher::monocypher` (+ Ed25519 module) |
| LZ4 | `lz4` | tag `v1.10.0` | `ebb370ca83af` | BSD-2-Clause (`lib/` only; the CLI programs are GPL-2.0 and are not built) | wired: `lz4::lz4` |
| xxHash | `xxHash` | tag `v0.8.3` | `e626a72bc232` | BSD-2-Clause (library only; `xxhsum` is GPL-2.0 and is not built) | wired: `xxhash::xxhash` |

"wired" means `ThirdParty/CMakeLists.txt` already defines the CMake target; otherwise the named task wires it.
`GambitTests` contains a smoke test per wired target (`GambitTests/src/ThirdParty/ThirdPartyTests.cpp`) that pins
the expected version, so an accidental submodule bump fails CI.

## Notes for later tasks

- **NVRHI / Vulkan-Headers.** NVRHI's `main` no longer carries Vulkan-Headers as a submodule; its CMake downloads
  Vulkan-Headers v1.4.352 with `FetchContent` at configure time. To keep builds hermetic and offline-capable the
  headers are pinned here as `Vulkan-Headers` at that same tag. T07 should create the `Vulkan::Headers` target from
  it (NVRHI uses an existing target) and set `NVRHI_FETCH_VULKAN_HEADERS=OFF`, `NVRHI_WITH_DX11=OFF`,
  `NVRHI_WITH_DX12=OFF`.
- **glslang.** Building glslang with the optimizer needs SPIRV-Tools and SPIRV-Headers, which upstream normally fetches
  with `update_glslang_sources.py` into `glslang/External/` (that would modify the submodule). T07 must either pin
  those two as extra submodules at the revisions in `glslang/known_good.json` or build glslang with
  `ENABLE_OPT=OFF`.
- **Updating a dependency.** `git -C ThirdParty/<name> fetch --depth 1 origin tag <tag>`, check it out, stage the
  submodule, update the table above and the smoke test that pins its version, and add a DECISIONS entry if the
  change is more than a patch bump.

## Source derived from other projects

Besides the submodules, some engine source files are ports of permissively licensed code. Their license notices are kept
in the file headers.

| Files | Origin | License |
|---|---|---|
| `Gambit/src/Gambit/Math/DMathTrig.cpp`, `DMathInverseTrig.cpp`, `DMathExpLog.cpp`, `DMathFmod.cpp`, `Internal/DMathExpLogTables.cpp` | musl libc 1.2.6 `src/math` (which derives from FreeBSD msun / Sun fdlibm, and from ARM's Optimized Routines for exp, log, log2 and pow) | MIT (musl), Sun fdlibm permission notice, MIT (ARM) |

`Gambit::DMath` (D-011) is the only user: the ports exist because simulation code must not call the platform libm.
