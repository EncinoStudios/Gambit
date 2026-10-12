#!/usr/bin/env python3
"""Runs the doctest executable (and the CTest smoke tests). Quiet: failures + one summary line.

    python3 scripts/test.py [--config Debug] [--filter "<doctest test-case pattern>"] [--no-gpu]

--filter takes a doctest -tc pattern, e.g. "Log: *". --no-gpu sets GB_TEST_NO_GPU=1 (skips the "gpu" suite).
Full logs: build/logs/test-<config>.log and build/logs/ctest-<config>.log
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import _common as common

SUMMARY_PATTERN = re.compile(
    r"test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed\s*\|\s*(\d+) skipped")
ASSERTION_PATTERN = re.compile(r"assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed")
CTEST_PATTERN = re.compile(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common.add_config_argument(parser)
    parser.add_argument("--filter", default="", metavar="PATTERN", help="doctest test-case filter")
    parser.add_argument("--no-gpu", action="store_true", help="skip tests in the 'gpu' suite")
    return parser.parse_args()


def run_doctest(arguments: argparse.Namespace, env: dict[str, str]) -> tuple[bool, str]:
    config = arguments.config
    binary = common.build_dir(config) / "bin" / common.executable("GambitTests")
    if not binary.exists():
        common.fail(f"test: {common.relative(binary)} not found. Run: python3 scripts/build.py --config {config}")

    command = [str(binary), "--no-colors"]
    if arguments.filter:
        command.append(f"--test-case={arguments.filter}")
    log_path = common.LOG_DIR / f"test-{config}.log"
    code = common.run_logged(command, log_path, env=env)
    lines = common.read_log(log_path)

    summary = next((m for m in map(SUMMARY_PATTERN.search, reversed(lines)) if m), None)
    assertions = next((m for m in map(ASSERTION_PATTERN.search, reversed(lines)) if m), None)
    if code != 0 or summary is None:
        print(f"test: FAILED {config} (exit code {code}, log: {common.relative(log_path)})", file=sys.stderr)
        common.print_excerpt(lines[1:], limit=80)  # skip the echoed command line
        return False, ""

    cases, _passed, _failed, skipped = (int(group) for group in summary.groups())
    text = f"{cases} test cases"
    if assertions:
        text += f", {assertions.group(1)} assertions"
    if skipped:
        text += f", {skipped} skipped"
    return True, text


def run_ctest(arguments: argparse.Namespace, env: dict[str, str]) -> tuple[bool, str]:
    config = arguments.config
    log_path = common.LOG_DIR / f"ctest-{config}.log"
    command = ["ctest", "--test-dir", str(common.build_dir(config)), "--exclude-regex", "^GambitTests$",
               "--output-on-failure"]
    code = common.run_logged(command, log_path, env=env)
    lines = common.read_log(log_path)
    summary = next((m for m in map(CTEST_PATTERN.search, reversed(lines)) if m), None)
    if code != 0 or summary is None:
        print(f"test: FAILED smoke tests of {config} (log: {common.relative(log_path)})", file=sys.stderr)
        common.print_excerpt(lines[1:], limit=60)
        return False, ""
    total = int(summary.group(3))
    return True, f"{total} smoke tests" if total else ""


def main() -> int:
    arguments = parse_arguments()
    stopwatch = common.Stopwatch()
    env = common.prepare_environment()
    if arguments.no_gpu:
        env["GB_TEST_NO_GPU"] = "1"

    ok, text = run_doctest(arguments, env)
    if not ok:
        return 1
    parts = [text]

    if not arguments.filter:
        common.require_tool("ctest", "Install CMake 3.24 or newer.")
        ok, smoke = run_ctest(arguments, env)
        if not ok:
            return 1
        if smoke:
            parts.append(smoke)

    print(f"test: OK {arguments.config} - {', '.join(parts)} in {stopwatch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
