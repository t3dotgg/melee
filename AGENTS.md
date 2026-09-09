# Melee for Mac

This is Theo's fully automated slop experiment, based on `doldecomp/melee`.
It is not meant for serious use or investigation. No support, maintenance, or
human review is promised. Do not present it as an official port or maintained
project.

Work only on Theo's explicit requests. Do not start an unsolicited audit,
investigation, cleanup, or upstream contribution. When Theo requests work,
use the rules below and keep the slop notice visible in the README, app,
contribution guidance, and pull request descriptions.

## Repository boundary

- Push only to `t3dotgg/melee4mac` or a local branch.
- Never open a pull request, issue, review, or comment on `doldecomp/melee` for this work.
- Use `--repo t3dotgg/melee4mac` for GitHub CLI mutations. Check the remote URL before pushing.
- The fork's default branch is `master`. Rebase onto its latest commit before opening a pull request.
- Open real pull requests within the fork when a review record is useful. Do not open drafts.
- Keep the automation notice in the README and contribution rules.

## Changes

- Read `.github/CONTRIBUTING.md` and the relevant source and callers first.
- Give each parallel agent a separate worktree and an explicit list of owned files.
- Prefer small, evidence-based changes. Do not guess the meaning of a field or function.
- Preserve known original names, data layouts, exported symbols, and gameplay behavior during cleanup.
- Local variable names, private helpers, known SDK types, and useful comments are good starting points.
- Retain compiler workarounds that matching requires. Explain the reason near the code or in `docs/code/`.
- Keep gameplay experiments separate from cleanup branches.
- Write in plain English. State what was checked and what remains uncertain.

## Verification

- Follow `docs/build-and-run.md`. Configure matching work with `--no-always-apply` to avoid automatic symbol-file writes.
- For direct ARM64 work, use the native compile inventory, sanitizer tests, and actual native game runs. Verify match startup, input, rendering, sound, match end, and return to the menu.
- Do not run the Intel compiler comparison or install or use Rosetta for the direct ARM64 port.
- For matching GameCube work, run `python tools/verify.py` after every integrated batch that changes game code, headers, or build settings.
- The matching US v1.02 executable must keep SHA-1 `08e0bf20134dfcb260699671004527b2d6bb1a45`.
- Do not change `config/GALE01/build.sha1` or the original executable to make a cleanup pass.
- A progress report alone is not sufficient. Build and check the complete executable.
- Format only edited C and header files with the pinned clang-format version. Run the source checker on affected files.
- Add focused tests for tool behavior and failure cases. Avoid tests that only repeat the implementation.
- Include actual verification results and their limits in the commit or pull request record.
- Attribute automated pull requests to the verified public model and harness. If the model is unknown, name only the harness.

## Local files

- Never commit or upload game images, extracted game data, DOL files, or game build artifacts.
- Keep experiments and extracted files under ignored `build/` or `orig/` paths.
- Do not add private machine paths to documentation.
- Public CI has no original game data. Do not report its tool and style checks as a matching build.
