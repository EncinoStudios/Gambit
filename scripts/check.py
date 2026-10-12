#!/usr/bin/env python3
"""Runs everything required before a commit: format check, build, all tests. Quiet; stops at the first failure.

    python3 scripts/check.py [--config Debug] [--jobs N] [--no-gpu] [--skip-format]

--skip-format exists for CI jobs on platforms where the reference clang-format is not installed; the Linux job
always checks formatting.
"""

from __future__ import annotations

import argparse
import subprocess
import sys

import _common as common


def run_step(name: str, script: str, extra: list[str]) -> bool:
    result = subprocess.run([sys.executable, str(common.REPO_ROOT / "scripts" / script), *extra], check=False)
    if result.returncode != 0:
        print(f"check: FAILED at step '{name}'", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common.add_config_argument(parser)
    parser.add_argument("-j", "--jobs", type=int, default=0, metavar="N", help="parallel build jobs")
    parser.add_argument("--no-gpu", action="store_true", help="skip tests in the 'gpu' suite")
    parser.add_argument("--skip-format", action="store_true", help="do not run the clang-format check")
    arguments = parser.parse_args()

    stopwatch = common.Stopwatch()
    config = ["--config", arguments.config]
    build = config + (["--jobs", str(arguments.jobs)] if arguments.jobs else [])
    test = config + (["--no-gpu"] if arguments.no_gpu else [])

    steps = []
    if not arguments.skip_format:
        steps.append(("format", "format.py", ["--check"]))
    steps += [("build", "build.py", build), ("test", "test.py", test)]

    for name, script, extra in steps:
        if not run_step(name, script, extra):
            return 1

    print(f"check: OK ({', '.join(name for name, _, _ in steps)}) {arguments.config} in {stopwatch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
