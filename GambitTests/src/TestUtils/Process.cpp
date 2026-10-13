#include "TestUtils/Process.h"

#include "TestSupport.h"

#include "Gambit/Core/PlatformDetection.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#if defined(GB_PLATFORM_WINDOWS)
#	include <windows.h>
#else
#	include <fcntl.h>
#	include <signal.h>
#	include <sys/wait.h>
#	include <unistd.h>
#endif

namespace Gambit::Tests
{
	namespace
	{
		void WriteTextFile(const std::filesystem::path& path, const std::string& text)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream.write(text.data(), static_cast<std::streamsize>(text.size()));
		}

#if defined(GB_PLATFORM_WINDOWS)

		// The command line the C runtime of the program splits back into exactly these arguments.
		std::wstring QuoteArgument(const std::wstring& argument)
		{
			if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
			{
				return argument;
			}
			std::wstring quoted = L"\"";
			for (auto position = argument.begin();; ++position)
			{
				size_t backslashes = 0;
				while (position != argument.end() && *position == L'\\')
				{
					++position;
					++backslashes;
				}
				if (position == argument.end())
				{
					quoted.append(backslashes * 2, L'\\'); // before the closing quote
					break;
				}
				if (*position == L'"')
				{
					quoted.append(backslashes * 2 + 1, L'\\');
				}
				else
				{
					quoted.append(backslashes, L'\\');
				}
				quoted.push_back(*position);
			}
			quoted.push_back(L'"');
			return quoted;
		}

		std::wstring ToWide(const std::string& utf8)
		{
			if (utf8.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
			return wide;
		}

		// A handle the child inherits, on a file (or NUL).
		HANDLE OpenInheritable(const std::wstring& path, bool forWriting)
		{
			SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
			return CreateFileW(
				path.c_str(), forWriting ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				&attributes, forWriting ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		}

		std::string DescribeLastError()
		{
			return "Windows error " + std::to_string(GetLastError());
		}

		void Launch(
			const std::filesystem::path& executable, std::span<const std::string> arguments,
			const ProcessOptions& options, const std::filesystem::path& inputFile,
			const std::filesystem::path& outputFile, const std::filesystem::path& errorFile, ProcessResult& result)
		{
			const HANDLE input = OpenInheritable(inputFile.wstring(), false);
			const HANDLE output = OpenInheritable(outputFile.wstring(), true);
			const HANDLE errors = OpenInheritable(errorFile.wstring(), true);
			const auto closeAll = [&] {
				for (const HANDLE handle : { input, output, errors })
				{
					if (handle != INVALID_HANDLE_VALUE)
					{
						CloseHandle(handle);
					}
				}
			};
			if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE || errors == INVALID_HANDLE_VALUE)
			{
				result.StartError = "cannot open the standard streams: " + DescribeLastError();
				closeAll();
				return;
			}

			std::wstring commandLine = QuoteArgument(executable.wstring());
			for (const std::string& argument : arguments)
			{
				commandLine += L' ' + QuoteArgument(ToWide(argument));
			}
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdInput = input;
			startup.hStdOutput = output;
			startup.hStdError = errors;
			PROCESS_INFORMATION process{};
			const std::wstring directory = options.WorkingDirectory.wstring();
			const BOOL created = CreateProcessW(
				executable.wstring().c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
				directory.empty() ? nullptr : directory.c_str(), &startup, &process);
			closeAll();
			if (!created)
			{
				result.StartError = DescribeLastError();
				return;
			}
			result.Started = true;

			const DWORD milliseconds =
				static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(options.Timeout).count());
			if (WaitForSingleObject(process.hProcess, milliseconds) == WAIT_TIMEOUT)
			{
				TerminateProcess(process.hProcess, 1);
				WaitForSingleObject(process.hProcess, INFINITE);
				result.TimedOut = true;
			}
			DWORD code = 0;
			if (GetExitCodeProcess(process.hProcess, &code))
			{
				result.ExitCode = static_cast<int>(code);
			}
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}

#else

		constexpr auto PollInterval = std::chrono::milliseconds(5);

		void Launch(
			const std::filesystem::path& executable, std::span<const std::string> arguments,
			const ProcessOptions& options, const std::filesystem::path& inputFile,
			const std::filesystem::path& outputFile, const std::filesystem::path& errorFile, ProcessResult& result)
		{
			const int input = open(inputFile.c_str(), O_RDONLY);
			const int output = open(outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
			const int errors = open(errorFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
			// The child reports a failed exec through this pipe, which closes by itself when the exec succeeds.
			int report[2] = { -1, -1 };
			const bool prepared = input >= 0 && output >= 0 && errors >= 0 && pipe(report) == 0
				&& fcntl(report[1], F_SETFD, FD_CLOEXEC) == 0;
			const auto closeIfOpen = [](int descriptor) {
				if (descriptor >= 0)
				{
					close(descriptor);
				}
			};
			if (!prepared)
			{
				result.StartError = std::string("cannot prepare the standard streams: ") + std::strerror(errno);
				for (const int descriptor : { input, output, errors, report[0], report[1] })
				{
					closeIfOpen(descriptor);
				}
				return;
			}

			// Everything the child needs is prepared here: between fork and exec it may only call async-signal-safe
			// functions (the test process can have other threads).
			std::vector<std::string> words{ executable.string() };
			words.insert(words.end(), arguments.begin(), arguments.end());
			std::vector<char*> argv;
			for (std::string& word : words)
			{
				argv.push_back(word.data());
			}
			argv.push_back(nullptr);
			const std::string directory = options.WorkingDirectory.string();

			const pid_t child = fork();
			if (child == 0)
			{
				const bool ready = dup2(input, 0) >= 0 && dup2(output, 1) >= 0 && dup2(errors, 2) >= 0
					&& (directory.empty() || chdir(directory.c_str()) == 0);
				if (ready)
				{
					execv(argv[0], argv.data());
				}
				const int code = errno;
				[[maybe_unused]] const ssize_t written = write(report[1], &code, sizeof(code));
				_exit(127);
			}
			const int forkError = errno;
			for (const int descriptor : { input, output, errors, report[1] })
			{
				close(descriptor);
			}
			if (child < 0)
			{
				close(report[0]);
				result.StartError = std::string("fork failed: ") + std::strerror(forkError);
				return;
			}

			int execError = 0;
			ssize_t count = 0;
			do
			{
				count = read(report[0], &execError, sizeof(execError));
			} while (count < 0 && errno == EINTR);
			close(report[0]);
			const bool execFailed = count == static_cast<ssize_t>(sizeof(execError));
			result.Started = !execFailed;
			if (execFailed)
			{
				result.StartError =
					std::string("cannot start ") + executable.string() + ": " + std::strerror(execError);
			}

			const auto deadline = std::chrono::steady_clock::now() + options.Timeout;
			int status = 0;
			for (;;)
			{
				const pid_t done = waitpid(child, &status, WNOHANG);
				if (done == child)
				{
					break;
				}
				if (done < 0 && errno != EINTR)
				{
					status = 0;
					break;
				}
				if (std::chrono::steady_clock::now() >= deadline)
				{
					kill(child, SIGKILL);
					waitpid(child, &status, 0);
					result.TimedOut = true;
					break;
				}
				std::this_thread::sleep_for(PollInterval);
			}
			if (WIFEXITED(status))
			{
				result.ExitCode = WEXITSTATUS(status);
			}
			else if (WIFSIGNALED(status))
			{
				result.ExitCode = 128 + WTERMSIG(status);
			}
		}

#endif
	}

	ProcessResult RunProcess(
		const std::filesystem::path& executable, std::span<const std::string> arguments, const ProcessOptions& options)
	{
		ProcessResult result;
		// The streams are files: nothing has to read a pipe while the program runs, so a chatty program cannot block.
		const ScopedTempDirectory scratch;
		const std::filesystem::path inputFile = scratch.GetPath() / "input.txt";
		const std::filesystem::path outputFile = scratch.GetPath() / "output.txt";
		const std::filesystem::path errorFile = scratch.GetPath() / "errors.txt";
		WriteTextFile(inputFile, options.Input);

		Launch(executable, arguments, options, inputFile, outputFile, errorFile, result);
		if (result.Started)
		{
			result.Output = ReadTextFile(outputFile);
			result.Errors = ReadTextFile(errorFile);
		}
		return result;
	}
}
