# Iteration goals

How graphics devs and artists work in this codebase. Direction only — specifics change.

## North star

Time between "I changed something" and "I see result" measured in seconds, not minutes or restarts. Editing shader, swapping texture, tweaking constant never need closing app. Slow loop = people stop trying. Most of what makes game look good comes from trying things.

## Posture

Hold across all work, not just items below.

- **Cleanup runs ahead of new surface area.** Tripwires + methodology in `cleanup` skill (`.claude/skills/cleanup/SKILL.md`). Invoke when starting cleanup pass; auto-applies during ordinary editing on tripwire hit.
- **Soft fail.** Missing/broken asset shows error (stdout, or `DevLog` overlay), keeps loop running. Never crash dev loop.
- **Hot reload dev-only.** Release builds use packed archive, never watch disk. Guard call sites with `#if !defined(NDEBUG)`.
- **Single focus chain in UI.** At most one widget owns focus per frame (`ui_state.focusId`). Every focusable widget registers in chain on call; keyboard `Ui::Pump` cycles through, mouse click shifts focus to clicked widget, modal scope confines chain to modal contents. Topmost window under pointer (prev-frame z-order) absorbs background hits so widgets behind don't fire. Activation fires on `navActivate` while focused (keyboard) or click-release-while-over (mouse). Plain `Ui::Text` and other non-interactive paint do NOT enter chain.

## What's pending (priority order)

Top = next. Trim to top 5-7. Each item: rule/feature, **Why**, **How**, plus **Decided** / **Open** lines where ambiguity must survive context drop.

Active thrust: **WM workspace affinity & Terminal neovim shakedown**. Detect parent PID of windows to place them correctly; finalize terminal ANSI core, alt-screen, and conformance. Low-latency present mode shipped — captured neovim streams feed cleanly; release build interactive-smooth on 60Hz panel.

0. BUG TRACKER:
       [windows] terminal private buffer is not cleared on exit (neovim)
       [windows] terminal alt + arrow key input is broken
       [windows] terminal mouse scrolling doesn't work - page down, page up fine.
       [windows] terminal neovim !! quotes/braces teleport cursor wtf
       [windows] set title function set's only first character wtf
       [linux] set title is missing
       [neovim] did not detect dsr response from terminal
       terminal clipboard doesn't work

1. **WM workspace affinity** — detect parent process of new windows and place them in the same workspace. **Why:** if an agent or launcher starts an app in a specific workspace, it should stay there. **How:** query `_NET_WM_PID` on new windows, traverse process tree to find parent's workspace.

2. **Cleanup pass over `nyla/commons`** — invoke `cleanup` skill; concrete pending instances live there. **Why:** slop accumulates faster than gets fixed. **How:** `/cleanup`, sweep one tripwire at time.

2. Terminal — neovim support must; keep gating concern when prioritizing terminal work.
    * !! quotes/braces (any pair punctuation with highlight) + cursor annoyingly unreadable
    * part of neovim usability = emoji support, same for other apps
    * brings us to multiple font size support - retire bdf and try ttf?
    * need terminal benchmarks (reuse existing) since hardware powerful, can't stresstest in normal scenarios.
    * terminal app maybe good place to think about packing app builds. maybe works fine already?
    * mouse input nice and probably not hard, just not top priority

3. **Neovim usability shakedown** — launch `nvim` inside running terminal binary on Linux, fix what surfaces in actual use (paint glitches, cursor placement off-by-one, key sequence gaps, perceived hangs from missing replies). **Why:** offline parser replay silent on captured nvim streams, but interactive correctness (rendering, key emission, modifier handling under DECCKM, mouse if engaged) not yet observed end-to-end. **How:** `./bin/terminal`, `nvim`, exercise insert/normal/visual/command modes and `:help` scrolling. **Open:** visual mode highlighting in nvim — selection range not painted/tracked correctly (investigate SGR reverse vs. nvim's actual highlight protocol, possibly missing CSI handling); lag after `:q` (or any alt-screen app exit) before primary screen + new prompt visible — paint loop runs every frame and reads cells fresh, so delay upstream: prime suspect = nvim emitting DSR/DA queries on shutdown, stalling on missing reply (we swallow `c`/`n`/`t` finals); secondary suspect = shell-side IO scheduling. Escalate to actual emitter once measured; underline rendering (no atlas glyph yet — terminal_cell carries bit but paint is no-op).

4. **Terminal signal forwarding + Windows validation** — Ctrl-C reaches right place; Windows pty path unrun. **Why:** without Ctrl-C, foreground processes can't be interrupted cleanly under ConPTY (line discipline 0x03 only works on Linux). Windows code path needs end-to-end smoke. **How:** Linux relies on bash line discipline (textChars carries 0x03 already); add Windows-only branch keyed off `engine_frame.keyDown` to call `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)`; smoke-test `platform_pty_windows.cc` with `cmd.exe` running interactive program.

5. **Terminal conformance test pass** — run public terminal test suites against `terminal/`, reconcile stderr `LOG` lines to actual gaps. **Why:** ad-hoc nvim shakedown finds loud bugs but quiet conformance gaps (corner CSI params, edge scroll-region behavior, charset semantics) only surface under structured suite. **How:** install vttest (Dickey, `pacman -S vttest` on Arch / source `https://invisible-island.net/vttest/`), run `vttest` inside our terminal and walk menu (cursor, screen-feature, character-set, mouse, color, ECMA-48 CSI, reset) — any visible mismatch or LOG line = bug; complement with iTerm2 esctest (`https://gitlab.freedesktop.org/terminal-wg/esctest`) for finer ECMA-48 coverage. Capture findings as one bug per failing test. **Open:** which subset in scope (full suite huge — start with vttest screen-feature + cursor sections, defer mouse until ?1000-?1006 implemented).

6. **Pixel-precise paint backend** — second backend alongside `CellRenderer` for non-grid widgets (icons, sliders, color swatches, window chrome). **Why:** cell grid caps what GUI can express; pointer-precise hit areas + chrome outgrow it. **How:** widgets call `Ui::DrawText` / `Ui::DrawRect`, `Ui` dispatches on backend kind (no vtable); `CellRenderer` implements grid case, new backend handles textured quads + lines. **Decided:** dispatch via `Ui::Draw*` indirection (option b from review), not virtual paint interface.
7. **WM workspace affinity** — detect parent process of new windows and place them in the same workspace. **Why:** if an agent or launcher starts an app in a specific workspace, it should stay there. **How:** query `_NET_WM_PID` on new windows, traverse process tree to find parent's workspace.

Independent (not part of UI thrust; pick up between items or when blocking): GPU-side timing (Vulkan timestamp queries → artist-visible GPU cost), pipeline state migration to `breakout`/`3d_ball_maze`/`terminal`/`wm_overlay`/`renderer.cc`/`debug_text_renderer.cc`, blend state in `.pipeline` schema, `LOG` interleave from shader worker thread, `vkDeviceWaitIdle` hitch on shader reload.

Deferred until pixel backend lands: color editor, curve editor, animation timeline.