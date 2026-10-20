---
name: gambit-cli
description: Drive a Gambit project with the gambit CLI (and the command registry behind it) as a coding agent - create projects and scenes, edit entities, components, data tables and assets, simulate, check results, and leave comments for people and other agents. Use whenever you need to change or inspect a Gambit game project's content without the editor, or to leave a question, follow-up or review note on an entity, asset, file or feature.
---

# Drive a project with `gambit`

Everything the editor can do is a **command** (D-051); `gambit` runs one per call, `gambit batch` many in one session. The
full model, all commands and the file formats are in `docs/guides/Automation.md` and `docs/guides/FileFormats.md`. The
binary is `build/<Config>/bin/gambit` (see the build-and-test skill).

## Discover, do not guess

```sh
gambit commands                      # the 49 commands, one line each (add a prefix: gambit commands entity.)
gambit help entity create            # arguments, types, defaults, result, flags
gambit commands --json               # all schemas (what MCP tools are made from)
gambit component types --json        # every component: fields, types, defaults, ranges, enum values (scripts' too)
gambit component types --name Transform
```

Errors name the bad argument and what is valid ("did you mean --name?", "its fields are: ..."). Read them before retrying.

## The loop

```sh
gambit project info --project Games/Tetris           # or cd into the project: the .gproject there is used
gambit entity list --layout tree
gambit entity create --name Board --components '{"Transform": {"Translation": [0, 0, 0]}}'
gambit component set --entity Board --component Transform --values '{"Scale": [10, 20, 1]}'
gambit entity get --entity Board --json              # every component, exactly as in the scene file
gambit sim run --ticks 120 --components Transform    # headless; deterministic state hash
```

- **Each call is a session and saves what it changed** when it succeeds (`saved Scenes/Main.gscene`); a failed call saves
  nothing. `--dry-run` shows what would happen without writing. Use `--json` when you parse the output; exit codes: 0 ok,
  1 command failed, 2 usage error (fix the command line).
- The start scene is opened for you; `--scene Scenes/Level2.gscene` picks another. Make scenes with `scene new --path
  Scenes/Level2.gscene` (saved at the end of the call).
- **Entities** are a UUID (`0x...`, returned by `entity create` as `uuid`) or a unique name. Duplicate names make the name an
  error that lists the UUIDs; use them. UUIDs are random, so capture them from results rather than predicting them.
- **Partial values**: `component set --values '{"Weight": 0.5}'` changes only `Weight`; the same holds inside structs and
  lists of structs. Vectors are replaced whole (`"Translation": [0, 1, 0]`). Enums are value names (`"Fast"`), references and
  asset handles `"0x..."`.
- **Many edits at once**: `bulk get --component Health` returns a table (rows = entities), edit it, `bulk set --component
  Health --table '<json>'` applies it as one undo step; `bulk export-csv` (CSV text) or `bulk write-csv --path
  Exports/Health.csv` (a file; `--overwrite` to replace one) and `bulk import-csv` do the same through a spreadsheet. Data
  tables (`Data/*.csv`): `data get`, `data set`, `data add-row`, `data remove-row`.
- **Warnings matter.** A scene that holds components or fields this build does not know (a script component whose script
  is not loaded, data from a newer engine) loads with warnings, and saving it would delete that data: the save then fails
  with `conflict` and says what would be lost. Do not force it with `scene save --discard-unknown` unless losing that data
  is really intended; ask (leave a comment) when in doubt.
- **Several steps that belong together** (create a hierarchy, use selection or undo between steps) go in a batch: one JSON
  command per line, one session, all-or-nothing save:

  ```sh
  cat > build.jsonl <<'JSON'
  {"command": "entity.create", "args": {"name": "Rig"}}
  {"command": "entity.create", "args": {"name": "Arm", "parent": "Rig"}}
  {"command": "component.add", "args": {"entity": "Arm", "component": "Transform", "values": {"Translation": [1, 0, 0]}}}
  JSON
  gambit batch build.jsonl --json
  ```

  It stops at the first failing line and saves nothing; `--keep-going` continues. `-` reads standard input.
- **Check your work**: `entity get`/`list`, `sim run` (same scene and seed give the same `stateHash` on every run; a changed
  hash after an edit proves the edit reached the simulation), `log read --level warn` as a line of a batch (it shows the
  warnings of the commands before it; a new process has an empty log), and `gambit test` once T12 exists.
  `undo`/`redo` only work inside one session (a batch or the editor); to revert a saved change, edit again or use git.
- Files are text: commit scenes (`Scenes/*.gscene`), data (`Data/*.csv`), `*.meta` and `Comments/` like source.

## Leave comments (D-052)

Use comments for open questions, follow-ups and review notes instead of burying them in chat or in code comments. They are
files in `<Project>/Comments/`, shown by the editor and by `comment list`, and never exported.

```sh
export GAMBIT_AUTHOR=claude                          # who you are (default: user)
gambit comment add --kind Entity --entity Board --text "Is 10x20 right for the playfield?"
gambit comment add --kind File --file Scripts/Player.lua --line 12 --text "Off by one when the piece rotates?"
gambit comment add --kind Asset --asset Assets/Crate.png --text "Needs a normal map"
gambit comment add --kind Component --entity Board --component Transform --text "Scale is a placeholder"
gambit comment add --kind Feature --feature scoring --text "Open question: combo bonus?"
gambit comment add --kind Position --position '[3, 0, 5]' --text "Spawn point here?"
gambit comment list --status Open                    # what is waiting for an answer
gambit comment reply --id 0x... --text "Done in the last commit."
gambit comment resolve --id 0x... --text "Fixed."
```

Check `comment list --status Open` at the start of a task: someone may have left you instructions. Kinds: `Entity`,
`Component`, `Asset`, `File` (+ `--line`), `Feature` (a free tag), `Position` (`--position '[x, y, z]'`). Reopen a thread
with `comment reopen`; delete only what you wrote yourself.

## Rules

- Do not edit scene, prefab or meta files by hand when a command exists; commands validate and keep the formats stable.
  Editing text files is fine for scripts (`Scripts/*.lua`).
- Report a missing command instead of working around it (leave a `Feature` comment tagged `automation`): D-051 says every
  editor capability has a command.
- A value that begins with `--` must be written `--text=--odd`. Shell quoting is yours: put JSON in single quotes, or use
  `--args '<json object>'`, or a batch file.
