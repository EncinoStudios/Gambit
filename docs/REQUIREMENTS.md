# AI Game Engine

We're building a production-grade, simple and straight-forward 3D Game Engine for Windows, macOS and Linux (Ubuntu 24+). This is professional-grade software that needs to be stable, fully-tested, and real-world deployable. No hacks, no shortcuts - solid as a rock.

## Dev Workflow
- Ask questions only when absolutely necessary - work autonomously
- Testing is of the utmost importance - use lots of unit tests (any framework, simple is good), run automated testing
- Setup a scene used for testing, which tests every single feature in the engine - all components, and entire scripting API
- Do things properly, this is production-grade and not a hack project
- Create AGENTS.md and relevant skills to support development, and contain concrete development guidelines
- Code style to match [Hazel](https://docs.hazelengine.com/HazelForEngineers/DeveloperGuide#naming), full repo [here](https://github.com/TheCherno/Hazel)
- Create git repository, commit and push to [GitHub repo](https://github.com/EncinoStudios/Gambit). IMPORTANT: do a code review before committing, and make sure all changes comply with code style, production-grade quality standards, and have been properly tested/have unit tests that have passed where necessary

## Tech stack
- C++ and CMake for core engine
- [GLFW](https://github.com/glfw/glfw) and [nvrhi](https://github.com/NVIDIA-RTX/NVRHI), using Vulkan primarily on all platforms
- [glm](https://github.com/g-truc/glm) for math
- [miniaudio](https://github.com/mackron/miniaudio) for audio
- [ImGUI] (https://github.com/ocornut/imgui) for editor and debug UI only
- Lua for scripting, though this can be up for discussion

## Basic Architecture
- Static library for core engine, executable for editor and runtime (depending on chosen design)
- Need to have editor to build games - this can be embedded into runtime executable (and stripped from distribution builds), or can be a standalone executable
- Deterministic - by default, games made in the engine should be deterministic and always provide the same result if the same inputs are used at the same time. (Such that replay systems can work with just an input log)
- Strict ECS, to author scene with entities and components (Data and Logic of Entities, Components, and Systems, must be strictly separate. This rule does not apply to core engine functions)
- Ability to "export" game - executable that runs game without editing ability that we can distribute. 
- Optional export flag will allow game content to be hosted on a server and the executable will only contain core systems. This is to facilitate:
-- live service content where new game content should be able to exist without requiring the client to update for every content change 
-- prevent AI piracy/decompilation by not holding the entire game locally. This isn't DRM, this is adapting to powerful AI decompilation.
- Editor needs to be fully controllable by AI agents - I should be able to ask you to build me a game like Tetris, and you should have all the tools available to do so without my intervention
- Agents should have a way to leave comments for the user and each other referring to specific features, files, assets, or level content.

## 3D Renderer
- Import gltf meshes with materials and textures
- Editor has gizmo to position them in the world
- PBR material workflow
- IBL with HDRIs from https://polyhaven.com/hdris
- Soft shadow maps
- Good SSAO
- HDR pipeline with tonemapping
- Pixel perfect 2D functionality 
- Support vector artwork for 2D and UI

## 3D Physics
- Use simple 3D physics library of your choosing
- Controllable via scripting, authored via components in editor
- Works well with deterministic engine logic

## Animation and Rigging
- Support future pipeline tools for Houdini, Blender and Maya. glTF is the contract; DCC presets are documentation only
- Support for baked cloth physics
- FK and IK support
- Full animation support including layered partial animations and programatic animations

## Behavior
- Scripting with Lua (or other chosen language) - randomness must connect with determinism system regardless of choice. 
- Control entities/components and run in update loop, and entity destruction/creation
- Spawn new entities/prefabs
- Testing behavior should be able to be scripted directly into features or game content. (Should survive any CI/CD system)
- Game content should be stored in such a way where spreadsheet-like editors can be used to modify large amounts of game content without needing to individually edit each item. (Like modifying the stats of every weapon/enemy or easily swap all dialogue in a scene without needing to individually select entities)

## Demos
- Demo scenes of your choosing
- Simple 2 min walking sim with examples of dialogue branches
- Simple 1st/3rd person parkour sandbox
- Simple texas hold em that behaves like tabletop sim but different hands create particle effects eminating from the cards
- Simple destruction demo

