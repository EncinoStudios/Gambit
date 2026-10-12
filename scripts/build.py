#!/usr/bin/env python3
"""Configures (when needed) and builds Gambit with CMake presets. Quiet: errors + one summary line.

    python3 scripts/build.py [--config Debug|Release|Dist] [--target GambitTests] [--jobs N]

Pick the compiler with CC / CXX (for example CC=clang CXX=clang++). Switching compiler or configuration
options recreates the build directory automatically. Full logs: build/logs/build-<config>.log
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import _common as common

ERROR_LIMIT = 60  # distinct compiler errors printed when a build fails
ERROR_WIDTH = 300


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common.add_config_argument(parser)
    parser.add_argument("--target", action="append", default=[], metavar="NAME",
                        help="CMake target to build (repeatable; default: all)")
    parser.add_argument("-j", "--jobs", type=int, default=0, metavar="N",
                        help="parallel jobs (default: GB_JOBS or the CPU count)")
    parser.add_argument("-D", dest="definitions", action="append", default=[], metavar="VAR=VALUE",
                        help="extra CMake cache entry (repeatable); forces a re-configure")
    parser.add_argument("--no-werror", action="store_true", help="configure with GB_WARNINGS_AS_ERRORS=OFF")
    parser.add_argument("--clean", action="store_true", help="delete the build directory first")
    return parser.parse_args()


def toolchain_stamp(arguments: argparse.Namespace) -> dict[str, object]:
    """Everything that, when changed, requires a fresh build directory instead of an incremental one."""
    return {
        "CC": os.environ.get("CC", ""),
        "CXX": os.environ.get("CXX", ""),
        "definitions": sorted(arguments.definitions),
        "werror": not arguments.no_werror,
    }


def link_compile_commands(config: str) -> None:
    """Points <repo>/compile_commands.json at the build so editors and clangd find it (best effort)."""
    source = common.build_dir(config) / "compile_commands.json"
    link = common.REPO_ROOT / "compile_commands.json"
    if not source.exists():
        return
    try:
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(source)
    except OSError:
        pass  # Windows without symlink privileges: the file in the build directory is still usable.


def failure_excerpt(lines: list[str]) -> list[str]:
    """The part of a build log that explains a failure: from the first failed step on, else the tail."""
    for index, line in enumerate(lines):
        if line.startswith("FAILED:") or "CMake Error" in line:
            return lines[index:]
    return lines[-40:]


def report_failure(config: str, log_path, lines: list[str]) -> None:
    """Prints why a build failed: the distinct compiler errors (file:line), else the log from the first failed step."""
    failed_steps = sum(1 for line in lines if line.startswith("FAILED:"))
    errors, total = common.extract_compiler_errors(lines, ERROR_LIMIT)
    steps = f", {failed_steps} step(s) failed" if failed_steps else ""
    print(f"build: FAILED {config}{steps} (log: {common.relative(log_path)})", file=sys.stderr)
    if not errors:
        common.print_excerpt(failure_excerpt(lines))
        return
    for error in errors:
        print(error if len(error) <= ERROR_WIDTH else error[:ERROR_WIDTH] + " ...", file=sys.stderr)
    if total > len(errors):
        print(f"... ({total - len(errors)} more distinct errors in the log)", file=sys.stderr)


def main() -> int:
    arguments = parse_arguments()
    config = arguments.config
    stopwatch = common.Stopwatch()

    common.require_tool("cmake", "Install CMake 3.24 or newer.")
    common.require_tool("ninja", "Install Ninja (https://ninja-build.org).")
    env = common.prepare_environment()

    directory = common.build_dir(config)
    stamp_path = directory / ".gambit-toolchain.json"
    stamp = toolchain_stamp(arguments)

    stale = False
    if directory.exists():
        try:
            stale = json.loads(stamp_path.read_text(encoding="utf-8")) != stamp
        except (OSError, ValueError):
            stale = (directory / "CMakeCache.txt").exists()
    if directory.exists() and (arguments.clean or stale):
        shutil.rmtree(directory)

    log_path = common.LOG_DIR / f"build-{config}.log"
    configure_log = common.LOG_DIR / f"configure-{config}.log"

    if not (directory / "CMakeCache.txt").exists() or arguments.definitions:
        preset = f"{common.host_os()}-{config.lower()}"
        command = ["cmake", "--preset", preset, f"-DGB_WARNINGS_AS_ERRORS={'OFF' if arguments.no_werror else 'ON'}"]
        command += [f"-D{definition}" for definition in arguments.definitions]
        if common.run_logged(command, configure_log, env=env) != 0:
            print(f"build: FAILED to configure {config} (log: {common.relative(configure_log)})", file=sys.stderr)
            common.print_excerpt(common.read_log(configure_log))
            return 1
        stamp_path.write_text(json.dumps(stamp), encoding="utf-8")

    jobs = arguments.jobs or int(os.environ.get("GB_JOBS", "0")) or os.cpu_count() or 2
    command = ["cmake", "--build", str(directory), "--parallel", str(jobs)]
    for target in arguments.target:
        command += ["--target", target]
    # Ninja stops after the first failed step by default; "-k 0" builds everything that can still be built, so one run
    # (a CI step log) shows every failing translation unit instead of one per attempt.
    command += ["--", "-k", "0"]

    result = common.run_logged(command, log_path, env=env)
    lines = common.read_log(log_path)
    if result != 0:
        report_failure(config, log_path, lines)
        return 1

    link_compile_commands(config)
    warnings = sum(1 for line in lines if " warning: " in line or ": warning C" in line)
    suffix = f", {warnings} warning(s) - see {common.relative(log_path)}" if warnings else ""
    print(f"build: OK {config} ({common.compiler_description(config)}) in {stopwatch}{suffix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
