# Implementation protocol (used for every task)

You implement one Gambit task in your own git worktree (path and branch given in your prompt). Work only inside
that directory; never touch `/home/user/Gambit` itself or other worktrees.

1. **Resume first.** A previous agent may have been interrupted (API limits). Inspect `git status`,
   `git diff --stat` and untracked files before writing anything. Keep correct work, finish what is missing; never
   discard work blindly.
2. **Read**: your brief `docs/tasks/<ID>.md` (including any Follow-ups/Notes sections at its end — do follow-ups
   first), the review/notes files it references, the guides in `docs/guides/` relevant to it, the decisions it
   cites in `docs/DECISIONS.md`, and the skills in `.claude/skills/` it touches (build-and-test always).
3. **Build quietly** with the scripts. The machine has 4 CPUs shared with other agents: one build at a time, never
   more than `-j4`. Disk is limited (about 15 GB free, shared): keep one GCC Debug build dir; delete the Clang build
   dir after its check passes and any Release/Dist dirs once used.
4. **Quality**: production code per AGENTS.md; every deliverable real and tested; zero warnings; zero Vulkan
   validation errors for GPU work. New decisions go in `docs/DECISIONS.md` using only the ID range given in your
   prompt (other agents append in parallel).
5. **Done** = `python3 scripts/check.py` green with GCC and with Clang (`CC=clang CXX=clang++`, separate build dir as
   the build-and-test skill describes). Then COMMIT everything as one commit (subject ≤ 72 chars, imperative; body
   explains what/why citing the task and decisions) ending with these two lines, and do NOT push:
   ```
   Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
   Claude-Session: https://claude.ai/code/session_018Lf3htvbUciiJb4XMkzaDJ
   ```
6. **Report** (~150 words, final message): commit hash, summary, deviations from the brief, decisions needing
   confirmation, test case count, exact final check summary lines for GCC and Clang. Longer notes go to
   `docs/tasks/<ID>-notes.md`.

# Fix protocol (after review)

You receive reviewers' findings (file `docs/tasks/<ID>-review.md` in the worktree). For each finding: verify it is
real by reading code and, where possible, writing a test that fails before the fix; reject wrong findings with a
reason; fix every confirmed blocker/major/minor properly (regression test for bugs), nits when cheap; list genuine
design questions as open decisions instead of guessing. A previous fixer may have left uncommitted partial fixes —
inspect first and keep them. Run the full check with GCC and Clang, commit ONE commit
`Review fixes for <ID>: <short>` (body lists fixes, same trailer lines), do not push, and report: commit hash,
fixed (one line each), rejected (with reasons), open decisions, final check lines, test count.
