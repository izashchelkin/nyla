---
name: "review-before-acting"
description: "Before modifying code: investigate existing context, state findings honestly, ask for confirmation, then verify with build + tests + live hardware."
version: 1
created: "2026-06-03"
---
## When to Use
When asked to modify code, fix bugs, refactor, or implement features in the nyla codebase.

## Procedure
1. Investigate the current code and context before proposing changes.
2. State what you found and what you plan to do — be honest about tradeoffs.
3. Ask for confirmation before implementing, especially for architectural changes.
4. After implementing, verify: build, test (integration + live hardware awareness), review diff.
5. Commit only when confirmed working. Squash intermediate fix attempts.

## Pitfalls
- Don't implement without asking first — especially refactors, moves, or format changes.
- Don't trust AI-generated explanations of X11 protocol or RandR behavior without verification.
- Tests passing in Xvfb doesn't mean it works on real hardware.
- Moving code between files can introduce subtle regressions — prefer confirming the approach first.

## Verification
1. Build succeeds with cmake --preset linux-debug && cmake --build build/linux-debug --target wm
2. Integration tests pass: ./scripts/test_wm.sh
3. User confirms the change works on live hardware
4. No unexplained regressions or new bugs
