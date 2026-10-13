# Automation: commands, the `gambit` CLI, undo and comments

Everything an editor user can do, an agent can do (D-051). Both do it through the same API: **commands** in the
`CommandRegistry` (`Gambit/Automation`), executed against an `EditorContext` (the open project, scenes, selection, undo
history). The editor calls commands (D-050), the `gambit` CLI runs them from a shell, and the MCP server and the editor's
RPC endpoint (T22) will expose the same registry, so they cannot disagree about what a command does. The sections:
[the command model](#the-command-model), [the CLI](#the-gambit-cli), [commands by area](#commands-by-area),
[undo and redo](#undo-and-redo), [an example](#example-a-tiny-scene), [bulk editing](#bulk-editing-and-spreadsheets),
[comments](#comments), [writing a command](#writing-a-command).

## The command model

A command has a **dotted name** (`entity.create`, `data.add-row`; lower case, `-` inside a part), a one-line summary, an
**argument schema** and **flags**. `gambit commands --json` prints all of them; `gambit help entity create` prints one as
text. The schema is the subset of JSON Schema that MCP tools use: per argument a name (camelCase), a type (`string`,
`integer`, `number`, `boolean`, `array` with an item type, `object`, or any value), a description, `required`, `enum`
(strings), `default`, `minimum` and `maximum`; no other members are accepted (`additionalProperties: false`).

| Flag | Meaning |
|---|---|
| `mutating` | changes a scene, an asset or the project |
| `undoable` | the change goes on the undo stack (scene and data table edits) |
| `requires-project` / `requires-scene` | needs an open project / an active scene (checked before the handler runs) |
| `editor-only` | needs the running editor (viewport, play mode); refused in headless hosts |
| `writes-files` | writes project files right away, not only when scenes are saved: refused in a dry run |

`CommandRegistry::Execute(context, name, arguments)` checks the arguments against the schema (unknown names, missing
required arguments, wrong types, values outside an enum or range; whole floats such as `3.0` are accepted as integers, `null`
stands for "absent"), applies defaults, checks the flags' preconditions, runs the handler and returns a `CommandResult`:

```json
{ "ok": true, "message": "Created entity 'Crate' (0x7FDD41A81634EFF3)", "data": { "uuid": "0x7FDD41A81634EFF3", "...": "..." }, "warnings": [] }
{ "ok": false, "error": { "code": "not-found", "message": "there is no entity named 'Crat' in the active scene (entity.list shows the entities)" } }
```

No exception escapes `Execute`. Errors say which argument is wrong and what would be valid ("did you mean ..."). The codes:
`unknown-command`, `invalid-arguments` (the call does not match the schema or the values do not fit), `no-project`, `no-scene`,
`not-found`, `already-exists`, `conflict` (the state forbids it: unsaved changes, a cycle in the hierarchy, a component the
engine manages), `unsupported` (editor-only command in a headless host, file writes in a dry run), `failed` (file input and
output, content that does not fit) and `internal` (a bug).

Conventions shared by all commands:

- **Entities** are given as a UUID (`"0x7FDD41A81634EFF3"`) or as the name of the one entity that has it; two entities with
  the same name make the name an error that lists their UUIDs.
- **Paths** are project-relative with forward slashes (`Scenes/Level1.gscene`); `..` and absolute paths are refused. Scenes,
  prefabs and tables must be below one of the asset directories (`Assets`, `Scenes`, `Data`, `Scripts`).
- **Values** are the JSON of scene files (`docs/guides/FileFormats.md`): `[x, y, z]` vectors, enum value names, `"0x..."` for
  references and asset handles. `component.types` lists every component with its fields, types, defaults and enum values,
  script components included.
- **Partial edits.** `component.set`, `bulk.set` and CSV imports take partial values: a field that is not named keeps its
  value, and the same holds *inside* struct fields (`{"Layers": [{"Weight": 0.5}]}` changes only the weight of layer 0 and
  keeps its clip). Vectors and lists of numbers are replaced as a whole.
- **Random UUIDs.** Entities made by commands get random UUIDs (`UUIDMode::Random`, D-020): editing never consumes the
  scene's random generator. Two runs of the same script therefore write different UUIDs; refer to entities by name or capture
  the `uuid` of the result.

## The `gambit` CLI

```
gambit <command> [--project <dir|file.gproject>] [--scene <path>] [--args '<json>' | --<argument> <value>...] [--json] [--dry-run]
gambit batch <file.jsonl|-> [--project P] [--keep-going] [--dry-run] [--json]
gambit commands [<prefix>] [--json]
gambit help [<command>]
```

- `<command>` is the dotted name or the same words with spaces: `gambit entity create --name Crate` is
  `gambit entity.create --name Crate`. Global options may stand anywhere.
- `--project` names the project directory (it must hold exactly one `.gproject`) or the file; without it the project in the
  current directory is used. The project's start scene is opened and made active; `--scene <path>` opens another one instead.
- Arguments are options with the schema's names, camelCase or kebab-case (`--keep-world-transform`): `--name Crate`,
  `--name=Crate`, `--index 2`. Booleans stand alone (`--hashes`), take a value (`--hashes false`) or are negated
  (`--no-hashes`). Objects and arrays are JSON (`--components '{"Transform": {"Translation": [0, 1, 0]}}'`); an array of
  strings can also repeat the option (`--entities A --entities B`). A value that begins with `--` must be written
  `--text=--odd`. Values of "any" type (`data set --value`, `entity find --equals`) are JSON when they parse (`12.5`, `true`,
  `"7"`) and text otherwise (`Pistol`). `--args '<json object>'` gives all arguments as JSON; options override it. On Windows,
  mind your shell's quoting of JSON; `--args` and `batch` files avoid the problem.
- The command runs, and when it **succeeded** every scene and data table it changed is **saved** (`saved Scenes/Main.gscene`);
  a failed command saves nothing. `--dry-run` runs the command in memory and saves nothing; commands that write files right
  away (`writes-files`: `project.new`, `scene.save`, `asset.import`, `prefab.create`, `bulk.export-csv --path`) refuse.
  Comment commands work in memory in a dry run.
- Output is a short summary (the result's message, then the data as `key: value` lines and tables), or with `--json` the
  `CommandResult` document above (plus `"saved": [...]`). Warnings (unknown members in a loaded file) go to stderr.
- **Exit codes:** `0` ok, `1` the command failed (`not-found`, `conflict`, `failed`, ...; also a project or scene that cannot be
  opened), `2` usage error (unknown command, unknown option, an argument the schema rejects). With `--json` errors are
  printed as results on stdout and the exit code is the same.
- `GAMBIT_AUTHOR` names the author of comments (default `user`; `comment.add --author` overrides it).
- Each `gambit` call is one session: the undo history, selection and open scenes do not survive the process. Work that needs
  them (undo, selection, several commands on one unsaved state) belongs in a `batch`.

### Batch

`gambit batch --project P commands.jsonl` runs many commands in one process, in one session (selection and undo work between
lines). One JSON object per line, blank lines are skipped:

```
{"command": "entity.create", "args": {"name": "Rig"}}
{"command": "entity.create", "args": {"name": "Arm", "parent": "Rig", "components": {"Transform": {"Translation": [1, 0, 0]}}}}
{"command": "undo"}
```

It stops at the first failing line and then **saves nothing** (the batch happened as a whole or not at all, apart from the
files that `writes-files` commands wrote); `--keep-going` runs the remaining lines and saves what succeeded. The file `-` is
standard input. Output is one line per command (`[2] entity.create: Created entity 'Arm' ...`) and a summary, or with `--json`
one result per line (with `line` and `command`) and a final `{"summary": {...}}`. The exit code is the worst of the lines.
A batch may start with `project.new` and no `--project`.

## Commands by area

48 commands; `gambit commands` lists them, `gambit help <name>` documents one.

| Area | Commands |
|---|---|
| Project | `project.new` (folders, `.gproject`, `InputActions.json`, an empty start scene which is opened), `project.info` |
| Scenes | `scene.new` (in memory, written on save), `scene.open`, `scene.save` (`all`), `scene.close` (`discard`), `scene.list`, `scene.set-active` |
| Entities | `entity.create` (name, parent, index, components with initial values), `entity.destroy`, `entity.rename`, `entity.reparent` (index, `keepWorldTransform`; cycles refused), `entity.duplicate`, `entity.list` (flat or tree; filter by name, component, parent; paged), `entity.get` (every component as in a scene file), `entity.find` (by name, component, or a field's value) |
| Components | `component.add`, `component.remove`, `component.get`, `component.set` (partial values; `mergeKey` for drags), `component.types` |
| Selection, history | `selection.get`, `selection.set` (replace, add, remove), `undo`, `redo` (`steps`) |
| Assets | `asset.list`, `asset.info` (import settings, dependencies), `asset.import` (copies a file in and writes its `.meta`), `prefab.create`, `prefab.instantiate` |
| Data tables | `data.tables`, `data.get` (table, row or cell), `data.set`, `data.add-row`, `data.remove-row` |
| Bulk | `bulk.get`, `bulk.set`, `bulk.export-csv`, `bulk.import-csv` |
| Simulation, log | `sim.run`, `log.read` |
| Comments | `comment.add`, `comment.reply`, `comment.resolve`, `comment.reopen`, `comment.list`, `comment.get`, `comment.delete` |

Notes on behaviour that is easy to get wrong:

- `entity.destroy` removes the whole subtree; `entity.duplicate` copies it with new UUIDs, follows references between the
  copied entities and keeps references to others.
- Components the engine manages (`ID`, `Relationship`, `Inactive`, `WorldTransform`) cannot be added, removed or set, and
  only components that are removable can be removed (`Tag` cannot; `entity.rename` changes its `Name`). Read-only and
  runtime-only fields cannot be set. Adding `Disabled` switches the entity and its children off (`entity.get` shows
  `enabled` and `active`); removing it switches them on.
- `scene.close` refuses unsaved changes unless `discard` is set. `scene.new` fails if the file or an open scene has the path.
- `sim.run` simulates a **copy** of the active scene for `ticks` fixed ticks at the project's `SimulationHz`, headless, and
  returns the state hash (`stateHash`; with `hashes` the hash after every tick; with `components` a dump of those
  components). The copy starts exactly like the scene loaded from its file (`World::BeginSimulation`, D-020): same scene and
  seed, same hashes, in any process.
- `log.read` returns the host's recent log messages (`level`, `contains`, `count`); pass the `lastSequence` of one call as
  `after` to read on.
- `asset.import` and `prefab.create` write files at once and are not undoable; scene, component, entity and data-table edits
  are undoable.

## Undo and redo

Every mutating command that changes a scene or a data table is **one undo step**, however many things it changed (`bulk.set`
over 500 entities, `entity.destroy` of a subtree). Internally (`Automation/UndoStack.h`, `UndoOperations.h`) a step is an
`IUndoableOperation` (`Redo`, `Undo`, `GetDescription`) built from reflection and snapshots, so new components and commands
need no undo code:

- `SetFieldsOperation`: the fields of one component before and after (consecutive edits with the same merge key merge: a
  gizmo drag is one step);
- `ComponentPresenceOperation`: add or remove a component, with every field recorded on removal;
- `SubtreeOperation`: create (entity, duplicate, prefab instance) or destroy an entity with its descendants, by a snapshot that
  restores the same UUIDs at the same place among the siblings;
- `ReparentOperation`, `SetCellOperation`, `TableRowOperation` (a removed row returns to its position).

`UndoStack` adds **groups** (`BeginGroup`/`EndGroup`; nesting; a group of one operation is that operation; the registry wraps
each undoable command in one and rolls it back when the handler fails, so a failing command leaves the scene as it was),
**merging**, a bounded history (1000 steps) and **dirty tracking per document**: a scene or table is dirty when it differs
from what was saved, and becomes clean again when undone to the saved state. Undo and redo bring back the exact state: the
state hash after `undo` equals the one before the command. Operations name their document by project path, never by
pointer, so closing and reopening scenes is safe.

## Example: a tiny scene

```sh
gambit project new --directory Tiny --name Tiny
cd Tiny
gambit entity create --name Player --components '{"Transform": {"Translation": [0, 1, 0]}}'
gambit entity create --name Hat --parent Player
gambit component add --entity Hat --component Transform --values '{"Translation": [0, 0.5, 0]}'
gambit entity list --layout tree
gambit sim run --ticks 60                       # initialHash, stateHash: the same on every run
gambit comment add --kind Entity --entity Hat --text "Should the hat be a prefab?"
```

The same in one process, with `gambit batch --project Tiny build.jsonl`:

```
{"command": "entity.create", "args": {"name": "Player", "components": {"Transform": {"Translation": [0, 1, 0]}}}}
{"command": "entity.create", "args": {"name": "Hat", "parent": "Player", "components": {"Transform": {"Translation": [0, 0.5, 0]}}}}
{"command": "sim.run", "args": {"ticks": 60}}
```

`Tiny/Scenes/Main.gscene` now holds the two entities (UUIDs, `Parent`, components in name order; format in
`docs/guides/FileFormats.md`), `Tiny/Comments/0x....json` the comment. Commit them like any source file.

## Bulk editing and spreadsheets

D-027: one component type across all entities of a scene is a table, rows = entities (hierarchy order), columns = fields.

- `bulk.get --component Health [--fields '["Current"]'] [--limit N --offset M]` returns
  `{component, columns: [{name, type, readOnly}], rows: [{entity, name, values: [...]}], total}`; the cells are field values
  as in scene files. Columns follow the component's field order.
- `bulk.set --component Health --table '{...}'` takes the same shape back; the columns may be any subset (names or the
  column objects); rows hold `{entity, values}`. Everything is validated first (unknown entity, entity without the
  component, wrong types, read-only fields that would change), then applied as **one undo step**; cells that already hold
  their value change nothing.
- `bulk.export-csv` writes RFC 4180 CSV for a spreadsheet (to the text of the result, or to a `.csv` file of the project with
  `--path`): the first column `Entity` holds the UUIDs, `EntityName` the names (ignored on import); fields follow with a
  type in the header (`Health:float`); vectors, colors and quaternions get one column per part (`Translation_X`,
  `Color_R`), structs and arrays one column of JSON text. `bulk.import-csv --csv <text>` or `--path` reads it back with the same
  rules as `bulk.set`; the columns may be any subset (a vector needs all of its parts). Exporting and importing the
  same data changes nothing.
- Data tables (`Data/*.csv`, D-027) are separate: `data.tables`, `data.get`, `data.set --table Data/Weapons.csv --row pistol
  --column Damage --value 12.5` (JSON of the column's type, or text in CSV notation), `data.add-row`, `data.remove-row`. They
  are saved with the scene edits and undoable.

## Comments

D-052: agents and people leave notes for each other in the project: open questions, follow-ups, review notes. A **thread** is
one JSON file `<Project>/Comments/<id>.json` (`Id` is `"0x"` and 16 hex digits):

```json
{
  "Version": 1,
  "Id": "0x5C9A1D2E7B34F086",
  "Target": { "Kind": "Entity", "Scene": "Scenes/Main.gscene", "Entity": "0xC984D67C924C9E8C" },
  "Author": "claude",
  "Status": "Open",
  "Created": "2026-03-14T09:26:53Z",
  "Updated": "2026-03-14T09:30:02Z",
  "Messages": [
    { "Author": "claude", "Text": "Should the hat be a prefab?", "Timestamp": "2026-03-14T09:26:53Z" },
    { "Author": "user", "Text": "Yes.", "Timestamp": "2026-03-14T09:30:02Z" }
  ]
}
```

Target kinds (`Kind`): `Entity` (`Scene`, `Entity`), `Component` (+ `Component`), `Asset` (`Asset` path and/or `Handle`), `File`
(`File`, optional 1-based `Line`), `Feature` (a free `Feature` tag of up to 64 characters), `Position` (`Scene`, `Position`
`[x, y, z]`). Timestamps are UTC (wall clock; comments are not simulation). Comments are never part of exports and are not
assets (the Comments folder is not scanned). Files that are not valid threads are reported (as warnings when the project
opens) and left alone.

Commands: `comment.add --kind <Kind> --text ...` (with `--entity`, `--component`, `--scene-path`, `--asset`, `--file --line`,
`--feature` or `--position` as the kind needs; the entity is looked up in the active scene), `comment.reply`,
`comment.resolve` / `comment.reopen` (with an optional closing message), `comment.list` (filter by `--status`, `--kind`,
`--author`, `--scene-path`, `--entity`, `--component`, `--asset`, `--file`, `--feature`; oldest first), `comment.get`,
`comment.delete`. In C++: `CommentStore` (`Project/Comments.h`; `EditorContext::GetComments()`).

## Writing a command

See `.claude/skills/add-command/SKILL.md`. In short: a registration function in `Automation/Commands/`, a `CommandInfo` with
a schema and flags, a handler that returns a `CommandResult` and does every scene change through an undoable operation
(`context.Do(...)`), and tests in `GambitTests/src/Automation/` that cover the happy path, the error paths and `undo`
(the state hash returns to the value before).
