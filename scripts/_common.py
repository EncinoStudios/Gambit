"""Shared helpers for the Gambit developer scripts.

All scripts follow D-006: they print errors and one summary line; full logs go to build/logs/.
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = REPO_ROOT / "build"
LOG_DIR = BUILD_ROOT / "logs"
CONFIGS = ("Debug", "Release", "Dist")

# Sources that clang-format owns. ThirdParty and generated output are never formatted.
FORMAT_DIRECTORIES = ("Gambit/src", "GambitEditor/src", "GambitRuntime/src", "GambitCLI/src", "GambitTests/src")
FORMAT_SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".inl", ".mm")


def host_os() -> str:
    """Returns "linux", "windows" or "macos"; these are also the prefixes of the CMake presets."""
    system = platform.system()
    if system == "Linux":
        return "linux"
    if system == "Windows":
        return "windows"
    if system == "Darwin":
        return "macos"
    fail(f"unsupported host operating system: {system}")


def executable(name: str) -> str:
    return name + ".exe" if host_os() == "windows" else name


def build_dir(config: str) -> Path:
    return BUILD_ROOT / config


def normalize_config(value: str) -> str:
    for config in CONFIGS:
        if config.lower() == value.lower():
            return config
    raise argparse.ArgumentTypeError(f"unknown configuration '{value}' (choose from {', '.join(CONFIGS)})")


def add_config_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--config",
        type=normalize_config,
        default="Debug",
        metavar="|".join(CONFIGS),
        help="build configuration (default: Debug)",
    )


def fail(message: str, code: int = 1):
    print(message, file=sys.stderr)
    sys.exit(code)


def require_tool(name: str, hint: str) -> str:
    path = shutil.which(name)
    if path is None:
        fail(f"error: '{name}' was not found on PATH. {hint}")
    return path


def format_seconds(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:.1f}s"
    minutes, rest = divmod(int(round(seconds)), 60)
    return f"{minutes}m{rest:02d}s"


def relative(path: Path) -> str:
    try:
        return path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path)


def run_logged(command: list[str], log_path: Path, *, env: dict[str, str] | None = None, cwd: Path = REPO_ROOT) -> int:
    """Runs a command with all of its output going to log_path. Returns the exit code (-1 if it cannot start)."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        log.write("$ " + " ".join(command) + "\n")
        log.flush()
        try:
            return subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, cwd=cwd, env=env, check=False).returncode
        except OSError as error:
            log.write(f"cannot start '{command[0]}': {error}\n")
            return -1


def read_log(log_path: Path) -> list[str]:
    return log_path.read_text(encoding="utf-8", errors="replace").splitlines()


def print_excerpt(lines: list[str], limit: int = 60, width: int = 300) -> None:
    """Prints at most `limit` lines of at most `width` characters to stderr, keeping the head (the cause) and
    noting what was cut. Long lines are mostly echoed compiler command lines; the full log has them."""
    for line in lines[:limit]:
        print(line if len(line) <= width else line[:width] + " ...", file=sys.stderr)
    if len(lines) > limit:
        print(f"... ({len(lines) - limit} more lines)", file=sys.stderr)


# Compiler and linker diagnostics worth showing in a CI step log: GCC/Clang "file:line:col: error: text", MSVC
# "file(line): error C1234: text", linker messages without a location ("ld.lld: error: ...", "undefined reference to ...",
# "LNK2019: ...") and the driver's own "error:" lines.
_LOCATED_ERROR = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?:\d+:)? (?:fatal )?error: (?P<text>.*)$")
_MSVC_ERROR = re.compile(r"^(?P<file>.+?)\((?P<line>\d+)(?:,\d+)?\): (?:fatal )?error (?P<code>[A-Z]+\d+): (?P<text>.*)$")
_OTHER_ERROR = re.compile(r"^(?:\S+: (?:fatal )?error: .*|.*undefined reference to .*|.*\bLNK\d+: .*)$")


def extract_compiler_errors(lines: list[str], limit: int = 60) -> tuple[list[str], int]:
    """The distinct compiler/linker error lines of a build log, in order of first appearance, as (shown, total).

    A header error is repeated by every translation unit that includes it, so identical diagnostics are reported once.
    Paths below the repository root are shown relative to it. Context lines (notes, source excerpts) are dropped; the
    log has them.
    """
    seen: set[str] = set()
    errors: list[str] = []
    root = REPO_ROOT.as_posix() + "/"
    for raw in lines:
        line = raw.strip()
        if not (_LOCATED_ERROR.match(line) or _MSVC_ERROR.match(line) or _OTHER_ERROR.match(line)):
            continue
        msvc = _MSVC_ERROR.match(line)
        if msvc:  # the file in the portable spelling of the other compilers, without the column
            line = f"{msvc['file'].replace(chr(92), '/')}({msvc['line']}): error {msvc['code']}: {msvc['text']}"
        if line.startswith(root):
            line = line[len(root):]
        if line not in seen:
            seen.add(line)
            errors.append(line)
    return errors[:limit], len(errors)


def prepare_environment() -> dict[str, str]:
    """Environment for child processes. On Windows, imports the Visual Studio build environment when the
    MSVC compiler is wanted but not yet on PATH (so the scripts work from a plain terminal)."""
    env = os.environ.copy()
    if host_os() != "windows":
        return env

    wanted = (env.get("CXX") or "cl").lower()
    if not (wanted == "cl" or wanted == "cl.exe") or shutil.which("cl", path=env.get("PATH")):
        return env

    program_files = env.get("ProgramFiles(x86)") or env.get("ProgramFiles") or ""
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return env
    query = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True, text=True, check=False)
    install = query.stdout.strip()
    vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not install or not vcvars.exists():
        return env

    imported = subprocess.run(f'"{vcvars}" >nul && set', shell=True, capture_output=True, text=True, check=False)
    for line in imported.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            env[key] = value
    return env


def compiler_description(config: str) -> str:
    """"GNU 13.3.0" style description of the compiler a build directory was configured with."""
    for path in sorted(build_dir(config).glob("CMakeFiles/*/CMakeCXXCompiler.cmake")):
        text = path.read_text(encoding="utf-8", errors="replace")
        identity = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)', text)
        version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', text)
        if identity and version:
            return f"{identity.group(1)} {version.group(1)}"
    return "unknown compiler"


class Stopwatch:
    def __init__(self) -> None:
        self._start = time.monotonic()

    def elapsed(self) -> float:
        return time.monotonic() - self._start

    def __str__(self) -> str:
        return format_seconds(self.elapsed())
