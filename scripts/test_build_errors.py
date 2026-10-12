"""Unit tests of the build failure report (scripts/_common.py, scripts/build.py).

    python3 -m unittest discover -s scripts -p "test_*.py"
"""

from __future__ import annotations

import unittest

import _common as common

ROOT = common.REPO_ROOT.as_posix()


class ExtractCompilerErrorsTests(unittest.TestCase):
    def test_gcc_and_clang_errors_keep_file_and_line(self) -> None:
        lines = [
            "[1/9] Building CXX object Gambit/CMakeFiles/Gambit.dir/src/A.cpp.o",
            f"{ROOT}/Gambit/src/A.cpp:12:7: error: 'Foo' was not declared in this scope",
            "   12 |     Foo foo;",
            f"{ROOT}/Gambit/src/B.cpp:3:1: fatal error: Missing.h: No such file or directory",
            "ninja: build stopped: subcommand failed.",
        ]
        shown, total = common.extract_compiler_errors(lines)
        self.assertEqual(
            shown,
            [
                "Gambit/src/A.cpp:12:7: error: 'Foo' was not declared in this scope",
                "Gambit/src/B.cpp:3:1: fatal error: Missing.h: No such file or directory",
            ],
        )
        self.assertEqual(total, 2)

    def test_identical_errors_from_several_translation_units_are_reported_once(self) -> None:
        line = f"{ROOT}/Gambit/src/Common.h:5:9: error: expected ';'"
        shown, total = common.extract_compiler_errors([line, "noise", line, line])
        self.assertEqual(shown, ["Gambit/src/Common.h:5:9: error: expected ';'"])
        self.assertEqual(total, 1)

    def test_the_same_message_on_another_line_is_another_error(self) -> None:
        shown, total = common.extract_compiler_errors(
            [f"{ROOT}/a.cpp:1:1: error: bad", f"{ROOT}/a.cpp:2:1: error: bad"]
        )
        self.assertEqual(len(shown), 2)
        self.assertEqual(total, 2)

    def test_the_limit_caps_the_list_but_not_the_count(self) -> None:
        lines = [f"{ROOT}/f{index}.cpp:1:1: error: broken" for index in range(100)]
        shown, total = common.extract_compiler_errors(lines, limit=60)
        self.assertEqual(len(shown), 60)
        self.assertEqual(total, 100)
        self.assertEqual(shown[0], "f0.cpp:1:1: error: broken")

    def test_msvc_errors_use_forward_slashes_and_drop_the_column(self) -> None:
        shown, _ = common.extract_compiler_errors(
            [r"C:\repo\Gambit\src\A.cpp(42,9): error C2065: 'x': undeclared identifier"]
        )
        self.assertEqual(shown, ["C:/repo/Gambit/src/A.cpp(42): error C2065: 'x': undeclared identifier"])

    def test_linker_errors_without_a_location_are_kept(self) -> None:
        lines = [
            "/usr/bin/ld: A.o: in function `main': undefined reference to `Foo()'",
            "clang++: error: linker command failed with exit code 1 (use -v to see invocation)",
            "ld.lld: error: undefined symbol: Bar",
            "Hash.obj : error LNK2019: unresolved external symbol baz",
        ]
        shown, total = common.extract_compiler_errors(lines)
        self.assertEqual(total, 4)
        self.assertEqual(len(shown), 4)

    def test_warnings_notes_and_progress_lines_are_ignored(self) -> None:
        lines = [
            f"{ROOT}/a.cpp:1:1: warning: unused variable 'x'",
            f"{ROOT}/a.cpp:1:1: note: declared here",
            "[3/9] Linking CXX executable bin/GambitTests",
            "FAILED: Gambit/CMakeFiles/Gambit.dir/src/A.cpp.o",
        ]
        self.assertEqual(common.extract_compiler_errors(lines), ([], 0))

    def test_an_empty_log_has_no_errors(self) -> None:
        self.assertEqual(common.extract_compiler_errors([]), ([], 0))


if __name__ == "__main__":
    unittest.main()
