# Implementation protocol (used for every task)

Tasks run **one at a time in the main checkout** `/home/user/Gambit` on the branch `claude/zen-johnson-treki2`
(D-090). The container is ephemeral: anything not pushed can vanish at any moment (two finished tasks were lost
this way). So work in small green steps and push each one.

1. **Resume first.** A previous agent may have been interrupted (API limits). Run `git status`, `git log -5` and
   read `docs/tasks/<ID>-notes.md` if it exists before writing anything. Keep correct work, finish what is missing.
   If `ThirdParty/` is empty run `git submodule update --init --recursive --depth 1`.
2. **Read**: your brief `docs/tasks/<ID>.md` (including any Follow-ups/Notes sections at its end — do follow-ups
   first), the review/notes files it references, the guides in `docs/guides/` relevant to it, the decisions it
   cites in `docs/DECISIONS.md`, and the skills in `.claude/skills/` it touches (build-and-test always).
3. **Checkpoints.** Split the task into steps that each leave the tree green (the brief or your prompt may list
   them). After each step: self-review the diff against `.claude/skills/code-review/SKILL.md`, run
   `python3 scripts/check.py` (GCC), then commit (subject ≤ 72 chars, imperative, prefixed with the task ID, e.g.
   `T11: add sandboxed ScriptEngine`; body explains what/why) and **push at once**:
   `git push origin claude/zen-johnson-treki2` (retry up to 4 times with backoff on network errors). Keep a short
   running log of what is done and what remains in `docs/tasks/<ID>-notes.md` and commit it with each checkpoint,
   so a successor can continue. Never leave more than about an hour of work unpushed. Every commit message ends with:
   ```
   Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>
   Claude-Session: https://claude.ai/code/session_018Lf3htvbUciiJb4XMkzaDJ
   ```
4. **Build quietly** with the scripts (4 CPUs, `-j4` at most, one build at a time). Disk is limited: keep the GCC
   Debug build dir; delete the Clang build dir after its check passes and Release/Dist dirs once used.
5. **Quality**: production code per AGENTS.md; every deliverable real and tested; zero warnings; zero Vulkan
   validation errors for GPU work. New decisions go in `docs/DECISIONS.md` with the next free IDs in the range
   your prompt gives.
6. **Done** = every deliverable of the brief implemented, `python3 scripts/check.py` green with GCC **and** with
   Clang (`CC=clang CXX=clang++`, separate build dir as the build-and-test skill describes), a Release build
   compiling warning-free, everything committed and pushed, ROADMAP status of the task set to `review`.
7. **Report** (~150 words, final message): last commit hash, summary, deviations from the brief, decisions needing
   confirmation, test case count, exact final check summary lines for GCC and Clang.

# Review protocol (independent reviewer, after implementation)

The task's commits are on the branch (find them with `git log --grep '^<ID>:'`). Review them against the brief,
`AGENTS.md`, `docs/DECISIONS.md`, `docs/tasks/REVIEW.md` and the code-review skill, through three lenses in turn:
(1) correctness and robustness — trace concrete failure scenarios, error paths, lifetimes, determinism leaks;
(2) brief coverage and architecture — nothing stubbed, decisions respected, APIs fit the later tasks that depend on
them (read those briefs); (3) tests, style and hygiene. Verify each suspected problem (read the code; where possible
write a test that fails before the fix). Fix every confirmed blocker/major/minor properly with a regression test,
nits when cheap; list genuine design questions as open decisions instead of guessing. Work in checkpoints exactly
as above (`<ID> review: <what>` commits, pushed at once). When finished: check green with GCC and Clang, ROADMAP
status `done`, findings summarized in `docs/tasks/<ID>-review.md`, pushed. Report (~150 words): verdict, what was
fixed, what was rejected and why, open decisions, final check lines, test count.
