# File formats

Everything a project stores is plain text that people and agents can read, diff and edit (D-025): JSON for scenes,
prefabs, the project and asset metadata, CSV for data tables. The code is in `Gambit/Serialization` (JSON),
`Gambit/Asset` and `Gambit/Project`; the formats come from the reflection data (D-022), so a new component needs no
file code.

## JSON conventions

- UTF-8, 2-space indent, LF, a trailing newline. Keys keep the order in which the engine writes them (fields in
  registration order), so saving an unchanged file gives the same bytes and diffs stay small.
- Every format has `"Version"`, a positive integer. Files from older engines are upgraded by migration steps before they
  are read (`MigrationTable`); a file from a newer engine is refused.
- Reading is lenient where it is safe and strict where it is not. **Unknown members** (fields, components, top-level
  keys) are warnings and are ignored; **missing members** take their defaults; **malformed values** are errors that
  start with the JSON pointer of the problem (`/Entities/3/Components/Transform/Rotation: expected 4 numbers, found 3`)
  and nothing is loaded. Loaders never throw and never half-load.
- JSON has no comments.

### Values

| Field type | JSON |
|---|---|
| `bool` | `true` / `false` |
| `int32`, `uint32`, `int64`, `uint64` | integers (a whole float such as `3.0` is accepted when reading) |
| `float`, `double` | numbers, written with the shortest text that reads back exactly (`0.1`, `1.0`, `1e-05`); `NaN`, `Infinity` and `-Infinity` as strings |
| `Vec2`, `Vec3`, `Vec4` | `[x, y]`, `[x, y, z]`, `[x, y, z, w]` |
| `Color3`, `Color4` | `[r, g, b]`, `[r, g, b, a]` (linear) |
| `Quat` | `[x, y, z, w]` |
| `String` | a string (valid UTF-8) |
| `Entity`, `AssetHandle`, `UUID` | `"0x00000000DEADBEEF"` (16 hex digits when written; 1 to 16 are read; the null UUID is `"0x0000000000000000"`) |
| enum | the name of the value: `"Slow"` |
| struct | an object with one member per field |
| array | an array of the element values |

## `.gscene`

```json
{
  "Version": 1,
  "Name": "Level 1",
  "Seed": 42,
  "Settings": { "Gravity": [0.0, -9.81, 0.0] },
  "Entities": [
    {
      "UUID": "0x0000000000000001",
      "Components": {
        "Health": { "Current": 80.0, "Maximum": 100.0 },
        "Tag": { "Name": "Player" },
        "Transform": { "Translation": [1.0, 2.0, 3.0], "Rotation": [0.0, 0.0, 0.0, 1.0], "Scale": [1.0, 1.0, 1.0] }
      }
    },
    {
      "UUID": "0x0000000000000002",
      "Parent": "0x0000000000000001",
      "Components": { "Disabled": {}, "Tag": { "Name": "Hat" } }
    }
  ]
}
```

- `Seed` seeds the world's random generator. `Settings` is an object for scene-wide settings of the modules; the owner of
  each setting reads and writes its own member, nothing else interprets it.
- `Entities` are in hierarchy pre-order: roots in root order, each followed by its descendants, siblings in order. The
  order of the list **is** the sibling order, and `Parent` (left out for roots) is the only hierarchy information. The
  `ID` and `Relationship` components are not stored; neither are derived ones (`WorldTransform`) or `Inactive`, which the
  world recomputes from `Disabled` and the hierarchy.
- Components appear in name order, fields in registration order. A component that is not listed is not on the entity; an
  entity without a `Tag` is called `Entity`. Tags are `{}`.
- Script components (D-023) are components like the others, provided their schema is registered before the scene loads.
  A component whose type is unknown is a warning and is dropped, so open scenes after loading the scripts.
- Fields marked `NotSerialized` are runtime state and are not stored.

Loading (`SceneSerializer::Load`) checks everything first: UUIDs must be valid and unique, parents must exist, no cycles,
every value must fit its field; then it creates the entities in hierarchy pre-order whatever the order of the file, so
the order in which systems see entities depends on the structure only. Saving, loading and saving again gives the same
bytes, and the loaded world has the same state hash (`World::ComputeStateHash`) as the saved one. A file holds an
initial state: the seed, not the position of the random generator, the tick or the time.

## `.gprefab`

The same entity format, for one subtree (D-026):

```json
{
  "Version": 1,
  "Name": "Crate",
  "Entities": [
    { "UUID": "0x0000000000000001", "Components": { "Tag": { "Name": "Crate" } } },
    { "UUID": "0x0000000000000002", "Parent": "0x0000000000000001", "Components": { "Tag": { "Name": "Lid" } } }
  ]
}
```

The first entity is the single root. A prefab refers to no entity outside itself and to no other prefab:
`PrefabUtils::CreateFromEntity` clears references to other entities and drops `PrefabInstance` components, and loading
a file clears such references with a warning. Instantiating
(`PrefabUtils::Instantiate(world, prefab, parent, transform)`) gives every entity a new UUID (from the world's random
generator, so simulations stay deterministic), follows the references between the prefab's entities (also those inside
structs and arrays), places the root under `parent` with `transform` as its local transform, and adds a
`PrefabInstance` component `{ "Prefab": handle, "SourceEntity": uuid in the prefab file }` to every entity.

## `.gproject`

The reflected struct `ProjectSettings` after `"Version"`. A new project has these values (`Project::New`):

```json
{
  "Version": 1,
  "Name": "Demo",
  "StartScene": "Scenes/Main.gscene",
  "Directories": { "Assets": "Assets", "Scripts": "Scripts", "Data": "Data", "Scenes": "Scenes", "Comments": "Comments" },
  "SimulationHz": 60,
  "DefaultSeed": 1,
  "InputActions": "InputActions.json",
  "Rendering": { "Environment": "0x0000000000000000", "Tonemapper": "ACES", "Exposure": 1.0 },
  "Export": { "OutputName": "Demo", "Compress": true }
}
```

Paths are relative to the project directory, with forward slashes; the five directories are distinct. `SimulationHz`
is 1 to 1000. `Tonemapper` is `ACES` or `PBRNeutral`. The file is named `<Name>.gproject`; tools accept the file or the
directory that holds exactly one. `Project::New` also creates the folders, a default `InputActions.json` and an empty
`Scenes/Main.gscene`, and never overwrites files that exist.

## `.meta`

Next to every asset file, `Assets/Crate.png.meta` gives the asset its handle (D-014), so that moving or renaming the file
does not break references:

```json
{
  "Version": 1,
  "Handle": "0x0000000000ABCDEF",
  "Type": "Texture",
  "ImportSettings": { "Srgb": true }
}
```

`ImportSettings` belong to the importer of the type (textures: the reflected struct `TextureImportSettings`,
`{ "Srgb": true, "GenerateMips": true, "Filter": "Linear", "WrapU": "Repeat", "WrapV": "Repeat" }`, `Filter` also
`Nearest`, wraps also `ClampToEdge` and `MirroredRepeat`; missing members take these defaults). Files that contain
assets (glTF models, D-038) list them with their handles, so that re-importing keeps references to them:

```json
{
  "Version": 1,
  "Handle": "0x00000000000A0001",
  "Type": "Model",
  "ImportSettings": {},
  "SubAssets": [
    { "Key": "Texture/Bark", "Type": "Texture", "Handle": "0x00000000000A0002" },
    { "Key": "Material/Bark", "Type": "Material", "Handle": "0x00000000000A0003" },
    { "Key": "Mesh/Trunk", "Type": "Mesh", "Handle": "0x00000000000A0004" }
  ]
}
```

The import adds the keys it sees for the first time; keys are never removed, so a mesh that disappears from the file and
comes back gets its old handle. Keys and handles are unique. Commit the `.meta` files together with the assets. A scan of the
project (`AssetManager::Scan`) finds every file whose name says it is an asset (`DetectAssetType`: `.gscene` `.gprefab`
`.png` `.jpg` `.tga` `.bmp` (textures), `.hdr` (environments), `.glb` `.gltf` (models), `.gmat` `.lua` `.wav` `.ogg`
`.ttf` `.svg` `.csv`, and `InputActions.json`) below the
asset directories (`Assets`, `Scenes`, `Data`, `Scripts`) and creates the `.meta` file of the ones that have none. It
warns about:

- **duplicate handles** (a file copied together with its `.meta`): the file that had the handle before the scan keeps
  it (on the first scan, the first file in path order), the other is not registered until it gets a new one
  (`AssetManager::AssignNewHandle`); nothing is rewritten on its own;
- **orphaned** `.meta` files whose asset is gone (never deleted);
- **unreadable** `.meta` files (the asset is not registered and the file is not touched);
- a `.meta` file without a handle (a new one is assigned and the settings are kept) or with another type than the file
  name says (the file name wins);
- asset files whose names are not portable project paths (a backslash, `:`, a reserved device name such as `aux.png`):
  they are not registered and get no `.meta` file until they are renamed.

## `.gmat`

A material (D-025): the reflected struct `Material` after `"Version"` (`Renderer/Material.h`):

```json
{
  "Version": 1,
  "BaseColor": [1.0, 1.0, 1.0, 1.0],
  "BaseColorTexture": "0x00000000000A0002",
  "Metallic": 0.0,
  "Roughness": 0.5,
  "MetallicRoughnessTexture": "0x0000000000000000",
  "NormalTexture": "0x0000000000000000",
  "NormalScale": 1.0,
  "OcclusionTexture": "0x0000000000000000",
  "OcclusionStrength": 1.0,
  "EmissiveColor": [0.0, 0.0, 0.0],
  "EmissiveTexture": "0x0000000000000000",
  "EmissiveStrength": 1.0,
  "AlphaMode": "Opaque",
  "AlphaCutoff": 0.5,
  "DoubleSided": false,
  "UVTiling": [1.0, 1.0],
  "UVOffset": [0.0, 0.0]
}
```

Colors are linear; textures are handles (base color and emissive textures hold sRGB colors, the others data:
roughness in green and metallic in blue, a tangent-space normal map, occlusion in red). `AlphaMode` is `Opaque`, `Mask`
or `Blend`; every texture uses `UV * UVTiling + UVOffset`. Values must be finite.

## glTF models (`.gltf`, `.glb`)

glTF 2.0 files are read as they are (no conversion step); `.gltf` files may use external buffers and images next to them
or base64 data URIs, `.glb` files embedded ones. The file is a `Model` asset; its meshes, materials and textures are
sub-assets keyed by their glTF names (D-038), meshes with one submesh per triangle primitive. Extensions: see D-045.
Generate test files with `tools/gen_test_gltf.py`.

## CSV data tables

`Data/Weapons.csv` (D-027) is RFC 4180 text in UTF-8 (other encodings are errors): comma separated; fields with commas, quotes or line breaks in double quotes
(`""` for a quote inside); CRLF or LF; an optional UTF-8 byte order mark (kept when the table is saved). The first row
names the columns, each with an optional type:

```csv
Id,Damage:float,Ammo,Automatic:bool,Name
pistol,12.5,12,false,Pistol
rifle,30,30,true,"The ""Big"" Rifle"
```

- The first column holds the **unique row ids** (text). The other column names are identifiers, so scripts can write
  `Data.Get("Weapons", "pistol").Damage`.
- Types: `int` (64-bit), `float` (a double: `0.1` stays `0.1`), `bool` (`true`/`false`, any case), `string`, `asset`
  and `entity` (`0x...` UUIDs; empty is null). Typed columns read an empty cell as the default (0, false, ""). **Untyped
  columns** are inferred from their cells: all canonical integers (`7`, `-3`, not `007`) is `int`, all `true`/`false` is
  `bool`, all numbers is `float`, anything else (also a column with an empty cell) is `string`. Inferred types are not
  written back.
- Blank lines are skipped. Every row has as many fields as the header. Errors name the file, line and column:
  `Data/Weapons.csv: line 7, column 12: column 'Damage': 'abc' is not a valid float`.
- Saving writes floats with a decimal point (`30.0`), so a column of whole numbers stays a float column.

`DataRef` components (`{ "Table": "0x...", "RowId": "pistol" }`) point at a row of a table by the table's handle.
