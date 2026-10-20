---
name: add-command
description: Add a command to the Gambit CommandRegistry (the API shared by the editor, the gambit CLI, the MCP server and agents) with its argument schema, an undoable implementation and tests. Use whenever a task adds an editor feature, a tool capability or anything an agent should be able to do, or changes the arguments or result of an existing command.
---

# Add a command

D-051: every editor capability is a `Command` first; the UI, the CLI (`gambit <name>`), the MCP server and the RPC endpoint
call it. Background and conventions: `docs/guides/Automation.md`. Existing commands to copy from:
`Gambit/src/Gambit/Automation/Commands/` (`EntityCommands.cpp` for scene edits, `DataCommands.cpp` for tables,
`AssetCommands.cpp` for files, `CommentCommands.cpp` for project data).

## 1. Name and shape

- Dotted lower-case name, group first: `entity.create`, `data.add-row` (`-` inside a part). Reuse a group if there is one.
  Do not use `project`, `scene`, `json`, `args`, `dryRun`, `help`, `keepGoing` as argument names (the CLI owns them);
  other names are camelCase and become `--kebab-case` options.
- Decide the **flags** honestly: `Mutating` (changes anything), `Undoable` (a scene or table edit that goes through
  `context.Do`), `RequiresProject`, `RequiresScene` (implies a project), `EditorOnly` (viewport, play mode),
  `WritesFiles` (writes project files at once: refused in dry runs). `Undoable` needs `Mutating` and a project or scene.
- The result is JSON an agent can act on: ids and names of what was created, counts, the new state. Put a one-line
  human summary in the message (`"Created entity 'Crate' (0x...)"`).

## 2. Register it

In a registration function in `Automation/Commands/<Group>Commands.cpp` (a new group: add `Register<Group>Commands` to
`BuiltinCommands.h/.cpp`; new files under `Automation/` are picked up by CMake):

```cpp
RegisterOrDie(registry,
    CommandInfo{
        .Name = "entity.rename",
        .Summary = "Change the name of an entity",
        .Arguments = { StringArg("entity", EntityHelp).Require(), StringArg("name", "The new name").Require() },
        .ResultDescription = "{ uuid, name, parent, enabled }",
        .Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
    },
    EntityRename);
```

Argument builders: `StringArg`, `IntegerArg`, `NumberArg`, `BooleanArg`, `ObjectArg`, `ArrayArg(name, itemType, ...)`,
`EnumArg`, `AnyArg`, then `.Require()`, `.WithDefault(json)`, `.WithRange(min, max)`, `.WithMinimum(min)`. The registry
checks types, enums, ranges, required and unknown arguments *before* your handler runs, and applies defaults, so the
handler reads `GetString(arguments, "name")` and friends without validating shapes again. Validate what the schema cannot
say (an entity exists, a path is inside the project, a value fits the field) and fail with a message that names the
argument and what would work. `RegisterOrDie` asserts the description is valid (`CommandInfo::Validate`).

## 3. Implement the handler

`CommandResult Handler(EditorContext& context, const Json& arguments)`; use `CommandSupport` (`Automation/Commands/
CommandSupport.h`): `ResolveEntity` (UUID or unique name), `FindEditableComponentType`, `DecodeFieldEdits` (partial,
struct-merging values), `DescribeEntity`, `Fail(code, message)`.

- **Scene and table edits go through an undoable operation**: `context.Do(CreateScope<SetFieldsOperation>(...))`,
  `SubtreeOperation::Create/Destroy`, `ComponentPresenceOperation`, `ReparentOperation`, `SetCellOperation`,
  `TableRowOperation` (`Automation/UndoOperations.h`). They work from reflection and snapshots, so a new component or
  field needs nothing. Write a new `IUndoableOperation` only for a change none of them can express: `Redo` performs it and
  records what `Undo` needs on first use, `GetDocuments` names the scene or table path, `CanMerge`/`Merge` are optional.
- The registry wraps every `Undoable` command in an undo group and rolls it back if the handler fails, so several
  operations are one step and a failure leaves the scene as it was. Check everything you can *before* the first `Do`.
- Never touch `Internal` components directly (generic reflection refuses); use `World` calls (`SetParent`, `SetEnabled`,
  `SetSiblingIndex`, `DestroyEntity`) through operations. Entities made by tools get `UUIDMode::Random` (D-020).
- Errors are `Fail(CommandErrorCode::NotFound, "...")` (`InvalidArguments`, `NotFound`, `AlreadyExists`, `Conflict`,
  `Unsupported`, `Failed`); `Fail(error)` keeps the code of an `Error` made with `MakeCommandError`. No exceptions: the
  registry catches them as `internal`, which is a bug report.
- Files go through the project's `VFS`; `WritesFiles` commands must not run in dry runs (the flag does that).
- Unknown members in loaded content are warnings: put them in `CommandResult::Warnings`.

## 4. Test it

`GambitTests/src/Automation/<Group>CommandsTests.cpp`, test names `"Automation: <command> <behaviour>"`, using
`AutomationFixture` (`AutomationFixture.h`: temp project, registry, context, test components):

- happy path: `fixture.Succeed("entity.rename", J(R"({"entity": "A", "name": "B"})"))` and check the data and the scene;
- every error path: `fixture.Reject(name, args, CommandErrorCode::NotFound)` and assert the message names the problem;
- **undo/redo**: `fixture.CheckUndoRedo([&] { ...the command... })` checks that undo returns the scene to the previous state
  hash and redo reproduces the changed one; also that the command is one step (`GetUndoHistory().size()`);
- failure atomicity: a command that fails half way leaves `fixture.Hash()` unchanged.
CLI behaviour (option parsing, output, exit codes) is tested in `GambitTests/src/CLI/`; a new argument type or a new kind of
result needs a case there. The schema export is checked by `gambit commands --json`.

## 5. Finish

`python3 scripts/format.py`, `python3 scripts/check.py` (also with `CC=clang CXX=clang++`). Add the command to the table in
`docs/guides/Automation.md` and, if agents should know a new workflow, to `.claude/skills/gambit-cli/SKILL.md`. Update the
count (`gambit commands` lists them). Once T22 exists the MCP tools follow from the schema; nothing else to register.
