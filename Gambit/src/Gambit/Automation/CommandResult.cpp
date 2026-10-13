#include "gbpch.h"

#include "Gambit/Automation/CommandResult.h"

#include "Gambit/Core/FileSystem.h"

namespace Gambit
{
	std::string_view ToString(CommandErrorCode code) noexcept
	{
		switch (code)
		{
			case CommandErrorCode::None:
				return "none";
			case CommandErrorCode::UnknownCommand:
				return "unknown-command";
			case CommandErrorCode::InvalidArguments:
				return "invalid-arguments";
			case CommandErrorCode::NoProject:
				return "no-project";
			case CommandErrorCode::NoScene:
				return "no-scene";
			case CommandErrorCode::NotFound:
				return "not-found";
			case CommandErrorCode::AlreadyExists:
				return "already-exists";
			case CommandErrorCode::Conflict:
				return "conflict";
			case CommandErrorCode::Unsupported:
				return "unsupported";
			case CommandErrorCode::Failed:
				return "failed";
			case CommandErrorCode::Internal:
				return "internal";
		}
		return "internal";
	}

	namespace
	{
		constexpr int CommandErrorBase = 1000;
	}

	Error MakeCommandError(CommandErrorCode code, std::string message)
	{
		return Error(std::move(message), CommandErrorBase + static_cast<int>(code));
	}

	CommandErrorCode GetCommandErrorCode(const Error& error, CommandErrorCode fallback)
	{
		if (error.Code >= CommandErrorBase
		    && error.Code <= CommandErrorBase + static_cast<int>(CommandErrorCode::Internal))
		{
			return static_cast<CommandErrorCode>(error.Code - CommandErrorBase);
		}
		return HasFileError(error, FileError::NotFound) ? CommandErrorCode::NotFound : fallback;
	}

	CommandResult CommandResult::FromError(const Error& error, CommandErrorCode fallback)
	{
		return Failure(GetCommandErrorCode(error, fallback), error.Message);
	}

	CommandResult CommandResult::Success(Json data, std::string message)
	{
		CommandResult result;
		result.Ok = true;
		result.Code = CommandErrorCode::None;
		result.Message = std::move(message);
		result.Data = std::move(data);
		return result;
	}

	CommandResult CommandResult::Failure(CommandErrorCode code, std::string message)
	{
		CommandResult result;
		result.Ok = false;
		result.Code = code;
		result.Message = std::move(message);
		return result;
	}

	Json CommandResult::ToJson() const
	{
		Json json = Json::object();
		json["ok"] = Ok;
		if (Ok)
		{
			if (!Message.empty())
			{
				json["message"] = Message;
			}
			json["data"] = Data;
		}
		else
		{
			Json error = Json::object();
			error["code"] = std::string(ToString(Code));
			error["message"] = Message;
			json["error"] = std::move(error);
		}
		if (!Warnings.empty())
		{
			json["warnings"] = Warnings;
		}
		return json;
	}
}
