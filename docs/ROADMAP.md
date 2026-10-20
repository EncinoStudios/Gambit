# Gambit Roadmap

Source requirements: `docs/REQUIREMENTS.md`. Design decisions: `docs/DECISIONS.md`.
Each task has a brief in `docs/tasks/<ID>.md` (demos T26–T30 share `docs/tasks/T26-T30.md`). Status: `todo` · `wip` · `done` · `blocked`.
A task is `done` only when it builds warning-free, all tests pass, it passed code review and it is pushed.

## M0 — Bootstrap
| ID | Task | Status |
|----|------|--------|
| T01 | Repo skeleton, CMake, ThirdParty submodules, quiet scripts, Core basics, test exe, CI | done |

## M1 — Core runtime & determinism
| T02 | Core: VFS, FileSystem, Buffer, UUID, Timer, Log sinks, Random (PCG32), DMath, hashing | done |
| T03 | Platform: Application loop, Window (GLFW), Input devices, InputFrame/InputActions/InputLog | done |

## M2 — ECS, reflection, serialization, automation core
| T04 | TypeRegistry reflection, World/Entity on EnTT, core components, system scheduler, command buffer, state hash | done |
| T05 | JSON scene/prefab serialization, Project files, AssetManager + .meta, DataTable (CSV) | done |
| T06 | CommandRegistry + EditorContext + undo/redo; `gambit` CLI skeleton; comments system | todo |

## M3–M4 — Rendering
| T07 | GraphicsDevice (NVRHI/Vulkan windowed+headless), glslang shader pipeline, readback, first golden test | done |
| T08 | Mesh/Texture/Material assets, glTF import, forward PBR, camera, lights, debug lines | done |
| T09 | IBL (HDR→cube, irradiance, prefilter, BRDF LUT), skybox, HDR + tonemapping + bloom | todo |
| T10 | Cascaded soft shadow maps (PCF/PCSS), SSAO (GTAO + bilateral blur) | todo |

## M5 — Scripting
| T11 | Lua 5.4 + sol2 runtime, deterministic sandbox, Component/System declarations, core API | todo |
| T12 | Lua test framework, `gambit test` (JUnit), API stubs generation | todo |

## M6–M8 — Physics, animation, audio
| T13 | Jolt physics: bodies, colliders, joints, queries, events, character controller, Lua API | todo |
| T14 | Animation: skeletons, clips, layered/masked/additive animator, GPU skinning | todo |
| T15 | IK (two-bone, FABRIK, look-at), procedural pose API, morph targets, baked cloth + bake tool | todo |
| T16 | Audio (miniaudio) components + Lua API | todo |

## M9 — 2D, vector, UI, particles
| T17 | Renderer2D, sprites, pixel-perfect camera, SDF text | todo |
| T18 | SVG vector artwork, ECS game UI | todo |
| T19 | Particle system (CPU-simulated deterministic, GPU-rendered) | todo |

## M10–M11 — Editor & agent control
| T20 | Editor shell: ImGui NVRHI backend, docking, viewport, hierarchy, inspector, gizmos, play mode | todo |
| T21 | Editor: content browser, console, data table + bulk editors, comments panel, replay controls | todo |
| T22 | Automation transports: MCP server, editor JSON-RPC, screenshots, full command coverage | todo |

## M12 — Export & live content
| T23 | Content packs, `gambit export`, GambitRuntime player | todo |
| T24 | Remote content mount, signing, `gambit serve`, live updates | todo |

## M13 — Feature test project
| T25 | `Projects/FeatureTest`: every component + full Lua API covered by Lua tests in CI | todo |

## M14 — Demos
| T26 | Walking sim with branching dialogue | todo |
| T27 | Parkour sandbox (1st/3rd person) | todo |
| T28 | Texas Hold'em tabletop with hand particle effects | todo |
| T29 | Destruction demo (Voronoi fracture) | todo |
| T30 | Agent-built Tetris (built only through CLI/MCP tools — validates D-051) | todo |

## M15 — Hardening
| T31 | DCC pipeline docs (Blender/Maya/Houdini), user guides, full review pass, CI on all platforms | todo |
