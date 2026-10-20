#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"
#include "Gambit/Core/LogSinks.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		constexpr int64_t MaxTicks = 1'000'000;

		CommandResult SimRun(EditorContext& context, const Json& arguments)
		{
			const SceneDocument& scene = *context.GetActiveScene();
			const TypeRegistry& types = context.GetTypeRegistry();

			std::vector<const ComponentInfo*> dumped;
			if (Has(arguments, "components"))
			{
				for (const Json& name : arguments.at("components"))
				{
					Result<const ComponentInfo*> info = FindComponentType(types, name.get<std::string>());
					if (!info)
					{
						return Fail(info.GetError());
					}
					dumped.push_back(*info);
				}
			}

			// The simulation runs on a copy that starts like the scene loaded from its file would (D-020), exactly as
			// editor play starts it.
			const uint64_t seed = Has(arguments, "seed") ? GetUnsigned(arguments, "seed") : scene.Scene->GetSeed();
			Result<Scope<World>> created = context.CreateSimulationWorld(*scene.Scene, seed);
			if (!created)
			{
				return Fail(created.GetError());
			}
			World& world = **created;

			const uint32_t hz = context.GetProject()->GetSettings().SimulationHz;
			const TimeStep step(1.0f / static_cast<float>(hz));
			const auto ticks = static_cast<uint64_t>(GetInteger(arguments, "ticks"));
			const bool recordHashes = GetBool(arguments, "hashes", false);

			Json data = Json::object();
			data["initialHash"] = HexText(world.ComputeStateHash());
			Json hashes = Json::array();
			for (uint64_t tick = 0; tick < ticks; ++tick)
			{
				world.Tick(step, SimulationMode::Play);
				if (recordHashes)
				{
					hashes.push_back(HexText(world.ComputeStateHash()));
				}
			}
			data["ticks"] = ticks;
			data["simulationHz"] = hz;
			data["seed"] = seed;
			data["tick"] = world.GetTick();
			data["time"] = world.GetTime();
			data["entityCount"] = world.GetEntityCount();
			const uint64_t finalHash = world.ComputeStateHash();
			data["stateHash"] = HexText(finalHash);
			if (recordHashes)
			{
				data["hashes"] = std::move(hashes);
			}

			if (!dumped.empty())
			{
				Json entities = Json::array();
				std::vector<Entity> pending;
				const std::vector<Entity> roots = world.GetRoots();
				pending.assign(roots.rbegin(), roots.rend());
				while (!pending.empty())
				{
					const Entity entity = pending.back();
					pending.pop_back();
					Json components = Json::object();
					for (const ComponentInfo* info : dumped)
					{
						if (const void* raw = info->GetRaw(world, entity.GetHandle()); raw != nullptr)
						{
							Result<Json> encoded = ComponentJson(*info, raw);
							if (!encoded)
							{
								return Fail(encoded.GetError());
							}
							components[info->GetName()] = std::move(*encoded);
						}
					}
					if (!components.empty())
					{
						Json entry = Json::object();
						entry["uuid"] = entity.GetUUID().ToString();
						entry["name"] = entity.GetName();
						entry["components"] = std::move(components);
						entities.push_back(std::move(entry));
					}
					const std::vector<Entity> children = world.GetChildren(entity);
					pending.insert(pending.end(), children.rbegin(), children.rend());
				}
				data["dump"] = std::move(entities);
			}
			return CommandResult::Success(
				std::move(data), std::format("Ran {} ticks at {} Hz: state hash {}", ticks, hz, HexText(finalHash)));
		}

		std::optional<LogLevel> ParseLevel(std::string_view name)
		{
			for (const LogLevel level :
			     { LogLevel::Trace, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Critical })
			{
				if (ToString(level) == name)
				{
					return level;
				}
			}
			return std::nullopt;
		}

		CommandResult LogRead(EditorContext& context, const Json& arguments)
		{
			const Ref<RingBufferSink> buffer = context.GetLogBuffer();
			if (buffer == nullptr)
			{
				return Fail(CommandErrorCode::Unsupported, "this host keeps no log history");
			}
			const LogLevel minimum = ParseLevel(GetString(arguments, "level")).value_or(LogLevel::Trace);
			const std::string contains = GetString(arguments, "contains");
			const auto count = static_cast<size_t>(GetInteger(arguments, "count"));

			std::vector<LogEntry> entries = Has(arguments, "after")
				? buffer->SnapshotAfter(static_cast<uint64_t>(GetInteger(arguments, "after")))
				: buffer->Snapshot();
			std::erase_if(entries, [&](const LogEntry& entry) {
				return entry.Level < minimum || !ContainsIgnoreCase(entry.Message, contains);
			});
			if (entries.size() > count)
			{
				entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(count));
			}

			Json list = Json::array();
			uint64_t lastSequence = Has(arguments, "after") ? static_cast<uint64_t>(GetInteger(arguments, "after")) : 0;
			for (const LogEntry& entry : entries)
			{
				Json item = Json::object();
				item["sequence"] = entry.Sequence;
				item["level"] = std::string(ToString(entry.Level));
				item["logger"] = entry.LoggerName;
				item["message"] = entry.Message;
				item["time"] = FormatTimestampUtc(entry.Time);
				list.push_back(std::move(item));
				lastSequence = std::max(lastSequence, entry.Sequence);
			}
			Json data = Json::object();
			data["count"] = list.size();
			data["entries"] = std::move(list);
			data["lastSequence"] = lastSequence;
			data["totalWritten"] = buffer->GetTotalWritten();
			return CommandResult::Success(std::move(data));
		}
	}

	void RegisterSimulationCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "sim.run",
				.Summary = "Simulate the active scene headless for a number of fixed ticks",
				.Description =
					"Runs a copy of the scene from its seed (the scene itself does not change) and returns the "
					"state hash, which is the same for the same scene and seed on every run (D-011).",
				.Arguments = { IntegerArg("ticks", "Number of fixed ticks to run (at the project's simulation rate)")
		                           .WithRange(0, MaxTicks)
		                           .Require(),
		                       UnsignedArg("seed", "Seed of the random generator (default: the scene's seed)"),
		                       BooleanArg("hashes", "Also return the state hash after every tick").WithDefault(false),
		                       ArrayArg(
								   "components", ArgumentType::String,
								   "Component types whose values to return for every entity that has them") },
				.ResultDescription =
					"{ ticks, simulationHz, seed, tick, time, entityCount, initialHash, stateHash, hashes?, dump? }",
				.Flags = CommandFlags::RequiresScene,
			},
			SimRun);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "log.read",
				.Summary = "Read the recent log messages of the host",
				.Arguments = { IntegerArg("count", "At most this many entries (the newest)")
		                           .WithRange(1, 1000)
		                           .WithDefault(100),
		                       EnumArg(
								   "level", { "trace", "info", "warn", "error", "critical" },
								   "Only this level and above")
		                           .WithDefault("trace"),
		                       IntegerArg(
								   "after",
								   "Only entries with a sequence number above this one (to read on from lastSequence)")
		                           .WithMinimum(0),
		                       StringArg("contains", "Only messages that contain this text (ignoring case)") },
				.ResultDescription =
					"{ count, entries: [{ sequence, level, logger, message, time }], lastSequence, totalWritten }",
				.Flags = CommandFlags::None,
			},
			LogRead);
	}
}
