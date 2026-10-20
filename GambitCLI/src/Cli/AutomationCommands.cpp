#include "Cli/AutomationCommands.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/CommandBatch.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>

namespace Gambit::Cli
{
	namespace
	{
		constexpr const char* AuthorVariable = "GAMBIT_AUTHOR";
		constexpr size_t MaxCellWidth = 48;
		constexpr size_t MaxInlineJson = 72;

		std::string Compact(const Json& json)
		{
			try
			{
				return json.dump(-1, ' ', false, Json::error_handler_t::replace);
			}
			catch (const std::exception&)
			{
				return "<unprintable>";
			}
		}

		std::string Pretty(const Json& json)
		{
			Result<std::string> text = DumpJson(json);
			return text ? std::move(*text) : Compact(json) + "\n";
		}

		std::string Truncate(std::string text, size_t width)
		{
			if (text.size() > width)
			{
				text.resize(width - 3);
				text += "...";
			}
			return text;
		}

		bool IsScalar(const Json& json)
		{
			return !json.is_object() && !json.is_array();
		}

		std::string FormatCell(const Json& value)
		{
			if (value.is_string())
			{
				return Truncate(value.get<std::string>(), MaxCellWidth);
			}
			if (value.is_null())
			{
				return "-";
			}
			if (value.is_array() && std::ranges::all_of(value, IsScalar))
			{
				std::string joined;
				for (const Json& item : value)
				{
					joined +=
						(joined.empty() ? "" : ",") + (item.is_string() ? item.get<std::string>() : Compact(item));
				}
				return Truncate(joined.empty() ? "-" : joined, MaxCellWidth);
			}
			return Truncate(Compact(value), MaxCellWidth);
		}

		bool IsTable(const Json& json)
		{
			return json.is_array() && !json.empty()
				&& std::ranges::all_of(json, [](const Json& row) { return row.is_object(); });
		}

		// A row with "children" (a tree such as entity.list --layout tree) is followed by its descendants, indented.
		bool IsChildList(std::string_view key, const Json& value)
		{
			return key == "children" && value.is_array();
		}

		void FlattenRows(const Json& rows, size_t depth, std::vector<std::pair<size_t, const Json*>>& flat)
		{
			for (const Json& row : rows)
			{
				flat.emplace_back(depth, &row);
				if (row.contains("children") && IsTable(row.at("children")))
				{
					FlattenRows(row.at("children"), depth + 1, flat);
				}
			}
		}

		std::string FormatTable(const Json& rows)
		{
			std::vector<std::pair<size_t, const Json*>> flat;
			FlattenRows(rows, 0, flat);

			std::vector<std::string> columns;
			for (const auto& [depth, row] : flat)
			{
				for (const auto& [key, value] : row->items())
				{
					if (!value.is_object() && !IsChildList(key, value)
					    && std::ranges::find(columns, key) == columns.end())
					{
						columns.push_back(key);
					}
				}
			}
			// The names of a tree carry the indentation, otherwise the first column does.
			const auto nameColumn = std::ranges::find(columns, "name");
			const size_t indented = nameColumn != columns.end() ? static_cast<size_t>(nameColumn - columns.begin()) : 0;

			std::vector<size_t> widths;
			for (const std::string& column : columns)
			{
				widths.push_back(column.size());
			}
			std::vector<std::vector<std::string>> cells;
			for (const auto& [depth, row] : flat)
			{
				std::vector<std::string> line;
				for (size_t index = 0; index < columns.size(); ++index)
				{
					std::string cell = row->contains(columns[index]) ? FormatCell(row->at(columns[index])) : "";
					if (index == indented)
					{
						cell.insert(0, depth * 2, ' ');
					}
					widths[index] = std::max(widths[index], cell.size());
					line.push_back(std::move(cell));
				}
				cells.push_back(std::move(line));
			}
			std::ostringstream out;
			const auto writeLine = [&](const std::vector<std::string>& line) {
				std::string text = "  ";
				for (size_t index = 0; index < line.size(); ++index)
				{
					text += line[index];
					if (index + 1 < line.size())
					{
						text.append(widths[index] - line[index].size() + 2, ' ');
					}
				}
				out << text << '\n';
			};
			writeLine(columns);
			for (const std::vector<std::string>& line : cells)
			{
				writeLine(line);
			}
			return out.str();
		}

		std::string Indent(const std::string& text, std::string_view prefix)
		{
			std::string indented;
			std::istringstream lines(text);
			for (std::string line; std::getline(lines, line);)
			{
				indented += std::string(prefix) + line + "\n";
			}
			return indented;
		}

		void PrintUsageError(
			const CommandContext& context, std::string_view program, std::string_view message,
			const CommandInfo* command)
		{
			context.Err << program << ": " << message << '\n';
			if (command != nullptr)
			{
				context.Err << "Run '" << program << " help " << command->Name << "' for its options.\n";
			}
		}

		// Prints the outcome of one command.
		void PrintResult(
			const CommandContext& context, std::string_view program, const CommandInfo* info,
			const CommandResult& result, bool asJson, const std::vector<std::string>& saved)
		{
			if (asJson)
			{
				Json document = result.ToJson();
				if (!saved.empty())
				{
					document["saved"] = saved;
				}
				context.Out << Pretty(document);
				return;
			}
			for (const std::string& warning : result.Warnings)
			{
				context.Err << "warning: " << warning << '\n';
			}
			if (!result.Ok)
			{
				context.Err << program << ": " << result.Message << '\n';
				if (info != nullptr && (result.Code == CommandErrorCode::InvalidArguments))
				{
					context.Err << "Run '" << program << " help " << info->Name << "' for its options.\n";
				}
				return;
			}
			const bool mutating = info != nullptr && info->HasFlag(CommandFlags::Mutating);
			if (!result.Message.empty())
			{
				context.Out << result.Message << '\n';
			}
			if ((!mutating || result.Message.empty()) && !result.Data.empty())
			{
				context.Out << FormatHuman(result.Data);
			}
			for (const std::string& path : saved)
			{
				context.Out << "saved " << path << '\n';
			}
		}

		std::string DefaultAuthor()
		{
			const char* name = std::getenv(AuthorVariable);
			return name != nullptr && *name != '\0' ? name : "user";
		}

		std::string DescribeType(const ArgumentInfo& argument)
		{
			if (!argument.Enum.empty())
			{
				std::string joined;
				for (const std::string& value : argument.Enum)
				{
					joined += joined.empty() ? value : "|" + value;
				}
				return argument.Type == ArgumentType::Array ? "[" + joined + "]" : joined;
			}
			if (argument.Type == ArgumentType::Array && argument.ItemType != ArgumentType::Any)
			{
				return std::format("{}[]", ToString(argument.ItemType));
			}
			return std::string(ToString(argument.Type));
		}
	}

	int GetExitCode(const CommandResult& result) noexcept
	{
		if (result.Ok)
		{
			return ExitSuccess;
		}
		return result.Code == CommandErrorCode::InvalidArguments || result.Code == CommandErrorCode::UnknownCommand
			? ExitUsage
			: ExitFailure;
	}

	std::string FormatHuman(const Json& data)
	{
		if (!data.is_object())
		{
			return Pretty(data);
		}
		std::string text;
		for (const auto& [key, value] : data.items())
		{
			if (IsScalar(value))
			{
				text += std::format("{}: {}\n", key, value.is_string() ? value.get<std::string>() : Compact(value));
			}
			else if (IsTable(value))
			{
				text += std::format("{}:\n{}", key, FormatTable(value));
			}
			else if (value.empty())
			{
				text += std::format("{}: {}\n", key, Compact(value));
			}
			else if (const std::string inlineText = Compact(value); inlineText.size() <= MaxInlineJson)
			{
				text += std::format("{}: {}\n", key, inlineText);
			}
			else
			{
				text += std::format("{}:\n{}", key, Indent(Pretty(value), "  "));
			}
		}
		return text;
	}

	std::string FormatCommandHelp(const CommandInfo& command, std::string_view programName)
	{
		std::ostringstream out;
		out << command.Name << " - " << command.Summary << "\n\n";

		std::string words = command.Name;
		std::ranges::replace(words, '.', ' ');
		out << "Usage: " << programName << ' ' << words;
		for (const ArgumentInfo& argument : command.Arguments)
		{
			const std::string option = std::format(
				"--{}{}", ToKebabCase(argument.Name),
				argument.Type == ArgumentType::Boolean ? "" : std::format(" <{}>", DescribeType(argument)));
			out << (argument.IsRequired ? " " + option : " [" + option + "]");
		}
		out << "\n       " << programName << ' ' << command.Name << " --args '<json object>'\n";

		if (!command.Description.empty())
		{
			out << '\n' << command.Description << '\n';
		}
		if (!command.Arguments.empty())
		{
			out << "\nOptions (the JSON argument names are in camelCase):\n";
			size_t width = 0;
			std::vector<std::string> heads;
			for (const ArgumentInfo& argument : command.Arguments)
			{
				heads.push_back(std::format("--{} <{}>", ToKebabCase(argument.Name), DescribeType(argument)));
				width = std::max(width, heads.back().size());
			}
			for (size_t index = 0; index < command.Arguments.size(); ++index)
			{
				const ArgumentInfo& argument = command.Arguments[index];
				out << "  " << std::left << std::setw(static_cast<int>(width)) << heads[index] << "  "
					<< argument.Description;
				if (argument.IsRequired)
				{
					out << " (required)";
				}
				if (argument.Default.has_value())
				{
					out << " (default: " << Compact(*argument.Default) << ")";
				}
				if (argument.Minimum.has_value() || argument.Maximum.has_value())
				{
					out << " ("
						<< (argument.Minimum.has_value() ? "min " + FormatNumber(*argument.Minimum) : std::string())
						<< (argument.Minimum.has_value() && argument.Maximum.has_value() ? ", " : "")
						<< (argument.Maximum.has_value() ? "max " + FormatNumber(*argument.Maximum) : std::string())
						<< ")";
				}
				out << '\n';
			}
		}
		if (!command.ResultDescription.empty())
		{
			out << "\nResult: " << command.ResultDescription << '\n';
		}
		const std::vector<std::string> flags = GetFlagNames(command.Flags);
		if (!flags.empty())
		{
			out << "Flags: ";
			for (size_t index = 0; index < flags.size(); ++index)
			{
				out << (index == 0 ? "" : ", ") << flags[index];
			}
			out << '\n';
		}
		out << "\nGlobal options: --project <dir|file.gproject>  --scene <path>  --args '<json>'  --json  --dry-run\n";
		return out.str();
	}

	// ---- AutomationProvider ----

	AutomationProvider::AutomationProvider(std::string programName, TypeRegistry& types)
		: m_ProgramName(std::move(programName)), m_Types(types)
	{
		RegisterBuiltinCommands(m_Registry);
	}

	Result<void> AutomationProvider::PrepareContext(
		EditorContext& context, const GlobalOptions& options, bool needsProject, bool needsScene,
		std::vector<std::string>& warnings) const
	{
		context.SetDryRun(options.DryRun);
		context.SetAuthorName(DefaultAuthor());

		if (options.Project.has_value())
		{
			if (Result<void> opened = context.OpenProject(FileSystem::FromUtf8(*options.Project)); !opened)
			{
				return Error::Format("cannot open the project '{}': {}", *options.Project, opened.GetError().Message);
			}
		}
		else if (needsProject)
		{
			// Without --project the project in the current directory, if there is one; commands that need a project say
			// so when there is none. A project file that is there but cannot be read is an error of its own.
			std::error_code error;
			const std::filesystem::path directory = std::filesystem::current_path(error);
			if (!error)
			{
				if (Result<void> opened = context.OpenProject(directory);
				    !opened && !HasFileError(opened.GetError(), FileError::NotFound))
				{
					return Error::Format(
						"cannot open the project in the current directory '{}': {}", FileSystem::ToUtf8(directory),
						opened.GetError().Message);
				}
			}
		}

		const auto opened = [&](const SceneDocument& scene) -> Result<void> {
			// What the load could not read (the loader has logged it) is also reported with the result: saving the
			// scene would drop it.
			warnings.insert(warnings.end(), scene.DroppedOnLoad.begin(), scene.DroppedOnLoad.end());
			return context.SetActiveScene(scene.Path);
		};
		if (options.Scene.has_value())
		{
			if (context.GetProject() == nullptr)
			{
				return Error("--scene needs a project: pass --project <dir|file.gproject>");
			}
			LoadReport report;
			Result<SceneDocument*> scene = context.LoadScene(*options.Scene, report);
			if (!scene)
			{
				return Error::Format("cannot open the scene '{}': {}", *options.Scene, scene.GetError().Message);
			}
			if (Result<void> active = opened(**scene); !active)
			{
				return active;
			}
		}
		else if (context.GetProject() != nullptr && context.GetActiveScene() == nullptr)
		{
			// The scene a project starts with is the one to work on unless another is named. Commands that need no
			// scene still get it, so that entities can be named in comments and the like.
			const std::string& start = context.GetProject()->GetSettings().StartScene;
			if (!start.empty() && context.GetProject()->GetVFS().Exists(start))
			{
				LoadReport report;
				Result<SceneDocument*> scene = context.LoadScene(start, report);
				if (scene)
				{
					if (Result<void> active = opened(**scene); !active)
					{
						return active;
					}
				}
				else if (needsScene)
				{
					return Error::Format("cannot open the start scene '{}': {}", start, scene.GetError().Message);
				}
				else
				{
					GB_CORE_WARN("cannot open the start scene '{}': {}", start, scene.GetError().Message);
				}
			}
		}
		return {};
	}

	std::optional<int> AutomationProvider::TryRun(std::span<const std::string> args, const CommandContext& context)
	{
		Result<SplitCommandLine> line = SplitGlobalOptions(args);
		if (!line)
		{
			PrintUsageError(context, m_ProgramName, line.GetError().Message, nullptr);
			return ExitUsage;
		}
		const std::optional<ResolvedCommand> command = ResolveCommand(m_Registry, line->Rest);
		if (!command.has_value())
		{
			// `gambit entity --help` lists the commands of the group.
			if (line->Options.Help && TryPrintHelp(line->Rest, context))
			{
				return ExitSuccess;
			}
			return std::nullopt;
		}
		if (line->Options.Help)
		{
			context.Out << FormatCommandHelp(*command->Info, m_ProgramName);
			return ExitSuccess;
		}
		const std::span<const std::string> options =
			std::span<const std::string>(line->Rest).subspan(command->WordCount);
		return RunCommand(*command, *line, options, context);
	}

	int AutomationProvider::RunCommand(
		const ResolvedCommand& command, const SplitCommandLine& line, std::span<const std::string> options,
		const CommandContext& context)
	{
		const GlobalOptions& global = line.Options;
		Result<Json> arguments = ParseCommandOptions(*command.Info, options, global.ArgsJson);
		if (!arguments)
		{
			if (global.AsJson)
			{
				context.Out << Pretty(
					CommandResult::Failure(CommandErrorCode::InvalidArguments, arguments.GetError().Message).ToJson());
			}
			else
			{
				PrintUsageError(context, m_ProgramName, arguments.GetError().Message, command.Info);
			}
			return ExitUsage;
		}

		EditorContext editor(m_Types);
		const bool needsScene = command.Info->HasFlag(CommandFlags::RequiresScene);
		const bool needsProject = needsScene || command.Info->HasFlag(CommandFlags::RequiresProject);
		CommandResult result;
		std::vector<std::string> loadWarnings;
		if (const Result<void> prepared = PrepareContext(editor, global, needsProject, needsScene, loadWarnings);
		    !prepared)
		{
			result = CommandResult::Failure(CommandErrorCode::Failed, prepared.GetError().Message);
		}
		else
		{
			result = m_Registry.Execute(editor, command.Info->Name, *arguments);
		}
		for (const std::string& warning : loadWarnings)
		{
			if (std::ranges::find(result.Warnings, warning) == result.Warnings.end()) // scene.open reports them too
			{
				result.Warnings.push_back(warning);
			}
		}

		std::vector<std::string> saved;
		if (result.Ok && !global.DryRun)
		{
			Result<std::vector<std::string>> written = editor.SaveAll();
			if (written)
			{
				saved = std::move(*written);
			}
			else
			{
				std::vector<std::string> warnings = std::move(result.Warnings);
				result = CommandResult::FromError(MakeCommandError(
					GetCommandErrorCode(written.GetError()),
					std::format("cannot save: {}", written.GetError().Message)));
				result.Warnings = std::move(warnings);
			}
		}
		PrintResult(context, m_ProgramName, command.Info, result, global.AsJson, saved);
		return GetExitCode(result);
	}

	bool AutomationProvider::TryPrintHelp(std::span<const std::string> words, const CommandContext& context)
	{
		if (const std::optional<ResolvedCommand> command = ResolveCommand(m_Registry, words); command.has_value())
		{
			context.Out << FormatCommandHelp(*command->Info, m_ProgramName);
			return true;
		}
		// A group such as `entity` lists its commands.
		std::string prefix;
		for (const std::string& word : words)
		{
			prefix += prefix.empty() ? word : "." + word;
		}
		const std::vector<const CommandInfo*> group = m_Registry.List(prefix + ".");
		if (group.empty())
		{
			return false;
		}
		context.Out << "Commands of '" << prefix << "':\n";
		size_t width = 0;
		for (const CommandInfo* info : group)
		{
			width = std::max(width, info->Name.size());
		}
		for (const CommandInfo* info : group)
		{
			context.Out << "  " << std::left << std::setw(static_cast<int>(width)) << info->Name << "  "
						<< info->Summary << '\n';
		}
		context.Out << "\nRun '" << m_ProgramName << " help <command>' for details on a command.\n";
		return true;
	}

	void AutomationProvider::PrintCommandList(std::ostream& out) const
	{
		const std::vector<const CommandInfo*> commands = m_Registry.List();
		size_t width = 0;
		for (const CommandInfo* info : commands)
		{
			width = std::max(width, info->Name.size());
		}
		out << "\nAutomation commands (write 'entity.create' or 'entity create'; options after the name):\n";
		std::string previousGroup;
		for (const CommandInfo* info : commands)
		{
			const std::string group = info->Name.substr(0, info->Name.find('.'));
			if (group != previousGroup && !previousGroup.empty())
			{
				out << '\n';
			}
			previousGroup = group;
			out << "  " << std::left << std::setw(static_cast<int>(width)) << info->Name << "  " << info->Summary
				<< '\n';
		}
		out << "\nOptions of automation commands:\n"
			<< "  --project <dir|file.gproject>  the project (default: the one in the current directory)\n"
			<< "  --scene <path>                 open this scene first (default: the project's start scene)\n"
			<< "  --args '<json>'                the arguments as a JSON object, instead of --name value options\n"
			<< "  --json                         print the result as JSON\n"
			<< "  --dry-run                      change nothing on disk\n"
			<< "Changed scenes and data tables are saved when the command succeeds. Exit codes: 0 ok, 1 the command "
			   "failed, 2 usage error.\n";
	}

	int AutomationProvider::RunCommands(std::span<const std::string> args, const CommandContext& context) const
	{
		Result<SplitCommandLine> line = SplitGlobalOptions(args);
		if (!line)
		{
			PrintUsageError(context, m_ProgramName, line.GetError().Message, nullptr);
			return ExitUsage;
		}
		if (line->Rest.size() > 1)
		{
			PrintUsageError(context, m_ProgramName, "'commands' takes at most one prefix", nullptr);
			return ExitUsage;
		}
		const std::string prefix = line->Rest.empty() ? std::string() : line->Rest.front();
		if (line->Options.AsJson)
		{
			Json document = Json::object();
			document["commands"] = m_Registry.ToJson(prefix);
			context.Out << Pretty(document);
			return ExitSuccess;
		}
		const std::vector<const CommandInfo*> commands = m_Registry.List(prefix);
		size_t width = 0;
		for (const CommandInfo* info : commands)
		{
			width = std::max(width, info->Name.size());
		}
		for (const CommandInfo* info : commands)
		{
			context.Out << std::left << std::setw(static_cast<int>(width)) << info->Name << "  " << info->Summary
						<< '\n';
		}
		return ExitSuccess;
	}

	int AutomationProvider::RunBatch(std::span<const std::string> args, const CommandContext& context)
	{
		Result<SplitCommandLine> line = SplitGlobalOptions(args);
		if (!line)
		{
			PrintUsageError(context, m_ProgramName, line.GetError().Message, nullptr);
			return ExitUsage;
		}
		if (line->Rest.size() != 1)
		{
			PrintUsageError(
				context, m_ProgramName,
				"'batch' takes one file of commands (or - for standard input), one JSON object per line", nullptr);
			return ExitUsage;
		}
		const GlobalOptions& options = line->Options;

		std::string text;
		if (line->Rest.front() == "-")
		{
			text.assign(std::istreambuf_iterator<char>(context.In), std::istreambuf_iterator<char>());
		}
		else
		{
			Result<std::string> content = FileSystem::ReadText(FileSystem::FromUtf8(line->Rest.front()));
			if (!content)
			{
				context.Err << m_ProgramName << ": cannot read '" << line->Rest.front()
							<< "': " << content.GetError().Message << '\n';
				return ExitFailure;
			}
			text = std::move(*content);
		}

		EditorContext editor(m_Types);
		std::vector<std::string> loadWarnings;
		if (const Result<void> prepared = PrepareContext(editor, options, true, true, loadWarnings); !prepared)
		{
			context.Err << m_ProgramName << ": " << prepared.GetError().Message << '\n';
			return ExitFailure;
		}
		if (!options.AsJson)
		{
			for (const std::string& warning : loadWarnings)
			{
				context.Err << "warning: " << warning << '\n';
			}
		}

		int exitCode = ExitSuccess;
		const auto print = [&](const BatchStep& step) {
			if (!step.Result.Ok)
			{
				exitCode = std::max(exitCode, GetExitCode(step.Result));
			}
			if (options.AsJson)
			{
				Json document = step.Result.ToJson();
				document["line"] = step.Line;
				document["command"] = step.Command;
				context.Out << Compact(document) << '\n';
				return;
			}
			const std::string label = std::format("[{}] {}", step.Line, step.Command.empty() ? "?" : step.Command);
			if (step.Result.Ok)
			{
				context.Out << label << ": " << (step.Result.Message.empty() ? "ok" : step.Result.Message) << '\n';
			}
			else
			{
				context.Out << label << ": error: " << step.Result.Message << '\n';
			}
			for (const std::string& warning : step.Result.Warnings)
			{
				context.Err << "warning: " << warning << '\n';
			}
		};
		const BatchSummary summary =
			RunCommandBatch(m_Registry, editor, text, BatchOptions{ .KeepGoing = options.KeepGoing }, print);

		size_t failed = summary.Failed;
		std::string saveNote;
		switch (summary.Saving)
		{
			case BatchSaving::Saved:
				break;
			case BatchSaving::Stopped:
				saveNote = "nothing was saved because a command failed (--keep-going continues and saves)";
				break;
			case BatchSaving::DryRun:
				saveNote = "dry run: nothing was saved";
				break;
			case BatchSaving::Failed:
				saveNote = std::format("cannot save: {}", summary.SaveError->Message);
				exitCode = std::max(exitCode, ExitFailure);
				++failed;
				break;
		}

		if (options.AsJson)
		{
			Json result = Json::object();
			result["executed"] = summary.Executed;
			result["failed"] = failed;
			result["saved"] = summary.Saved;
			if (!saveNote.empty())
			{
				result["note"] = saveNote;
			}
			if (!loadWarnings.empty())
			{
				result["warnings"] = loadWarnings;
			}
			Json document = Json::object();
			document["summary"] = std::move(result);
			context.Out << Compact(document) << '\n';
		}
		else
		{
			context.Out << summary.Executed << (summary.Executed == 1 ? " command" : " commands") << ", " << failed
						<< " failed";
			for (const std::string& path : summary.Saved)
			{
				context.Out << "; saved " << path;
			}
			context.Out << '\n';
			if (!saveNote.empty())
			{
				context.Err << m_ProgramName << ": " << saveNote << '\n';
			}
		}
		return exitCode;
	}

	void RegisterAutomationCommands(CommandDispatcher& dispatcher, TypeRegistry& types)
	{
		auto provider = CreateRef<AutomationProvider>(dispatcher.GetProgramName(), types);
		dispatcher.AddProvider(provider);

		const auto registerOrDie = [&dispatcher](Command command) {
			const std::string name = command.Name;
			const Result<void> registered = dispatcher.Register(std::move(command));
			GB_CORE_VERIFY(
				registered.HasValue(), "cannot register command '{}': {}", name, registered.GetError().Message);
		};
		registerOrDie(Command{
			.Name = "batch",
			.Summary = "Run many automation commands in one process, from a file with one JSON command per line",
			.Usage = "<file.jsonl|-> [--project <dir>] [--keep-going] [--dry-run] [--json]\n\nEach line is "
					 "{\"command\": \"entity.create\", \"args\": {\"name\": \"Crate\"}}. Stops at the first failure "
					 "(saving nothing) unless --keep-going. Changed scenes are saved at the end.",
			.Handler =
				[provider](const CommandContext& context, std::span<const std::string> args) {
					return provider->RunBatch(args, context);
				},
		});
		registerOrDie(Command{
			.Name = "commands",
			.Summary = "List the automation commands; with --json all their schemas",
			.Usage = "[<prefix>] [--json]",
			.Handler =
				[provider](const CommandContext& context, std::span<const std::string> args) {
					return provider->RunCommands(args, context);
				},
		});
	}
}
