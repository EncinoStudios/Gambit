#!/usr/bin/env python3
"""Formats (or with --check, verifies) C++ sources with clang-format using the repository's .clang-format.

    python3 scripts/format.py [--check]

Covers Gambit, GambitEditor, GambitRuntime, GambitCLI and GambitTests; ThirdParty is never touched.
Set CLANG_FORMAT to use a specific binary. The reference version is clang-format 18.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import _common as common

REFERENCE_MAJOR = 18
BATCH_SIZE = 40  # keeps the command line short enough for Windows
DIFF_PATTERN = re.compile(r"^(.+?):\d+:\d+: (?:error|warning): code should be clang-formatted", re.MULTILINE)


def find_clang_format() -> str:
    candidates = [os.environ.get("CLANG_FORMAT", ""), f"clang-format-{REFERENCE_MAJOR}", "clang-format"]
    for candidate in candidates:
        if candidate and (path := shutil.which(candidate)):
            return path
    common.fail("error: clang-format was not found. Install clang-format 18 or set CLANG_FORMAT.")


def version_of(binary: str) -> str:
    output = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False).stdout
    match = re.search(r"version (\d+)", output)
    return match.group(1) if match else "?"


def source_files() -> list[Path]:
    files: list[Path] = []
    for directory in common.FORMAT_DIRECTORIES:
        root = common.REPO_ROOT / directory
        if root.exists():
            files += [p for p in root.rglob("*") if p.is_file() and p.suffix in common.FORMAT_SUFFIXES]
    return sorted(files)


def batches(files: list[Path]):
    for start in range(0, len(files), BATCH_SIZE):
        yield [str(path) for path in files[start:start + BATCH_SIZE]]


def digest(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def check(binary: str, files: list[Path], stopwatch: common.Stopwatch) -> int:
    offenders: set[str] = set()
    for batch in batches(files):
        result = subprocess.run([binary, "--dry-run", "--Werror", "--style=file", *batch], capture_output=True,
                                text=True, cwd=common.REPO_ROOT, check=False)
        offenders.update(DIFF_PATTERN.findall(result.stderr))
        if result.returncode != 0 and not DIFF_PATTERN.search(result.stderr):
            print(result.stderr.strip() or "clang-format failed", file=sys.stderr)
            return 1

    if not offenders:
        print(f"format: OK ({len(files)} files) in {stopwatch}")
        return 0

    for name in sorted(offenders):
        print(f"needs formatting: {common.relative(Path(name))}", file=sys.stderr)
    version = version_of(binary)
    note = "" if version == str(REFERENCE_MAJOR) else f" (clang-format {version} differs from the reference {REFERENCE_MAJOR})"
    print(f"format: FAILED - {len(offenders)} of {len(files)} files need formatting{note}. "
          "Run: python3 scripts/format.py", file=sys.stderr)
    return 1


def reformat(binary: str, files: list[Path], stopwatch: common.Stopwatch) -> int:
    before = {path: digest(path) for path in files}
    for batch in batches(files):
        result = subprocess.run([binary, "-i", "--style=file", *batch], capture_output=True, text=True,
                                cwd=common.REPO_ROOT, check=False)
        if result.returncode != 0:
            print(result.stderr.strip() or "clang-format failed", file=sys.stderr)
            return 1
    changed = sum(1 for path in files if digest(path) != before[path])
    print(f"format: OK - reformatted {changed} of {len(files)} files in {stopwatch}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="only verify; exit 1 when a file would change")
    arguments = parser.parse_args()

    stopwatch = common.Stopwatch()
    binary = find_clang_format()
    files = source_files()
    if not files:
        print("format: OK (no files)")
        return 0
    return check(binary, files, stopwatch) if arguments.check else reformat(binary, files, stopwatch)


if __name__ == "__main__":
    sys.exit(main())
