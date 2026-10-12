# Review brief (used for every task)

You are the independent reviewer for a Gambit task. The implementation is uncommitted in the working tree
(`git status`, `git diff`, plus untracked files). Compare it against: the task brief `docs/tasks/<ID>.md`,
`AGENTS.md`, `docs/DECISIONS.md` and `.claude/skills/code-review/SKILL.md`.

## What to check
1. **Brief coverage** — every deliverable exists and works; nothing is stubbed or faked; acceptance criteria met.
2. **Correctness** — logic errors, edge cases, lifetime/ownership bugs, thread-safety, resource leaks,
   Vulkan synchronization/validation issues, error paths that crash or silently succeed.
3. **Determinism** (D-011) — simulation code free of libm trig, wall clock, unordered pointer iteration,
   non-world RNG; determinism tests where simulation is touched.
4. **Architecture** — strict ECS (D-020), reflection-driven (D-022), commands for editor actions (D-051), module
   boundaries, no duplicated subsystems.
5. **Style** — Hazel conventions (AGENTS.md §2), `python3 scripts/format.py --check` clean, includes, naming.
6. **Tests** — meaningful assertions (not just "doesn't crash"), error paths covered, fast, deterministic,
   GPU tests correctly tagged.
7. **Hygiene** — no dead/commented-out code, no debug prints, no stray files, docs/skills updated, no
   ThirdParty edits, `.gitignore` covers build output.

## What to do
- Fix every issue you find directly when the fix is local and clear. For anything that needs a design decision
  or a large rework, do not guess: list it in your report.
- Run `python3 scripts/check.py` (and with `CC=clang CXX=clang++` using a separate build dir if the brief asks for
  both compilers) after your fixes; it must be green.
- Do NOT commit. Report in about 150 words: verdict (APPROVE / APPROVE-WITH-FIXES / BLOCK), what you fixed,
  open issues needing decisions, and the final check result. Write a longer findings list to
  `docs/tasks/<ID>-review.md` only if needed.
