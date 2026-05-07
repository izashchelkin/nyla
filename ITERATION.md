# Iteration goals

How we want graphics developers and artists to work in this codebase. Direction only — the specifics will change.

## North star

The time between "I changed something" and "I see the result" should be measured in seconds, not in minutes or restarts. Editing a shader, swapping a texture, tweaking a constant should never require closing the app. If the loop is slow, people stop trying things — and most of what makes a game look good comes from trying things.

## Posture

These hold across all work, not just the items below.

- **Cleanup runs ahead of new surface area.** Tripwires and methodology live in the `cleanup` skill (`.claude/skills/cleanup/SKILL.md`). Invoke it when starting a cleanup pass; auto-applies during ordinary editing when a tripwire is hit.
- **Soft fail.** A missing or broken asset shows an error (stdout, or `DevLog` overlay) and keeps the loop running. Never crash the dev loop.
- **Hot reload is dev-only.** Release builds use the packed archive, never watch the disk. Guard call sites with `#if !defined(NDEBUG)`.
- **Single focus chain in UI.** At most one widget owns focus per frame (`ui_state.focusId`). Every focusable widget registers in the chain on call; keyboard `Ui::Pump` cycles through it, mouse click shifts focus to the clicked widget, modal scope confines the chain to the modal's contents. The topmost window under the pointer (prev-frame z-order) absorbs background hits so widgets behind don't fire. Activation fires on `navActivate` while focused (keyboard) or click-release-while-over (mouse). Plain `Ui::Text` and other non-interactive paint do NOT enter the chain.

## What's pending (priority order)

Top of list = next thing to work on. Trim to the top 5-7. Each item: rule/feature, **Why**, **How**, plus **Decided** / **Open** lines where ambiguity must survive a context drop.

Active thrust: **Terminal neovim shakedown**. Pty, ANSI core, alt-screen, special-key emission, line/char ops, scrollback storage + viewport paint, dynamic resize, DCS/intermediate/colon-subparam parser coverage, low-latency present mode all shipped — captured neovim streams feed cleanly via offline replay; release build now interactive-smooth on a 60Hz panel. Remaining: in-window neovim usability check, signal forwarding, Windows pty validation, conformance pass.

0. BUG TRACKER:
       [windows] terminal private buffer is not cleared on exit (neovim)
       [windows] terminal alt + arrow key input is broken
       [windows] terminal mouse scrolling doesn't work - page down, page up fine.
       [windows] terminal neovim !! quotes/braces teleport cursor wtf
       [windows] set title function set's only first character wtf
       [linux] set title is missing

1. **Cleanup pass over `nyla/commons`** — invoke the `cleanup` skill; concrete pending instances live there. **Why:** slop accumulates faster than it gets fixed. **How:** `/cleanup`, sweep one tripwire at a time.

2. Terminal — support for neovim is a must; keep it the gating concern when prioritizing terminal work.
    * !! quotes/braces (any pair punctuation with highlight) + cursor seems to be annoyingly unreadable
    * part of neovim usability is emoji support, the same for some other applications
    * we have to come with terminal benchmarks (reuse existing) since hardware is quite powerful and we cannot really stresstest in normal scenarios.
    * terminal application might also be a good place to think about packing application builds. does it maybe work already fine enough?
    * i would say mouse input is quite nice and probably not hard implement just not top priority

3. **Neovim usability shakedown** — launch `nvim` inside the running terminal binary on Linux and fix what surfaces in actual use (paint glitches, cursor placement off-by-one, key sequence gaps, perceived hangs from missing replies). **Why:** offline parser replay is silent on captured nvim streams, but interactive correctness (rendering, key emission, modifier handling under DECCKM, mouse if engaged) is not yet observed end-to-end. **How:** `./bin/terminal`, `nvim`, exercise insert/normal/visual/command modes and `:help` scrolling. **Open:** visual mode highlighting in nvim — selection range not painted/tracked correctly (investigate SGR reverse vs. nvim's actual highlight protocol, possibly missing CSI handling); lag after `:q` (or any alt-screen app exit) before primary screen + new prompt are visible — paint loop runs every frame and reads cells fresh, so the delay is upstream: prime suspect is nvim emitting DSR/DA queries on shutdown and stalling on the missing reply (we swallow `c`/`n`/`t` finals); secondary suspect is shell-side IO scheduling. Escalate to actual emitter once measured; underline rendering (no atlas glyph yet — terminal_cell carries the bit but paint is a no-op).

4. **Terminal signal forwarding + Windows validation** — Ctrl-C reaches the right place; Windows pty path is unrun. **Why:** without Ctrl-C, foreground processes can't be interrupted cleanly under ConPTY (line discipline 0x03 only works on Linux). Windows code path needs end-to-end smoke. **How:** Linux relies on bash line discipline (textChars carries 0x03 already); add a Windows-only branch keyed off `engine_frame.keyDown` to call `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)`; smoke-test `platform_pty_windows.cc` with `cmd.exe` running an interactive program.

5. **Terminal conformance test pass** — run public terminal test suites against `terminal/` and reconcile stderr `LOG` lines to actual gaps. **Why:** ad-hoc nvim shakedown finds the loud bugs but quiet conformance gaps (corner CSI params, edge scroll-region behavior, charset semantics) only surface under a structured suite. **How:** install vttest (Dickey, `pacman -S vttest` on Arch / source `https://invisible-island.net/vttest/`), run `vttest` inside our terminal and walk its menu (cursor, screen-feature, character-set, mouse, color, ECMA-48 CSI, reset) — any visible mismatch or LOG line is a bug; complement with iTerm2 esctest (`https://gitlab.freedesktop.org/terminal-wg/esctest`) for finer ECMA-48 coverage. Capture findings as one bug per test that fails. **Open:** which subset is in scope (full suite is huge — start with vttest screen-feature + cursor sections, defer mouse until ?1000-?1006 is implemented).

6. **Pixel-precise paint backend** — second backend alongside `CellRenderer` for non-grid widgets (icons, sliders, color swatches, window chrome). **Why:** cell grid caps what GUI can express; pointer-precise hit areas + chrome outgrow it. **How:** widgets call `Ui::DrawText` / `Ui::DrawRect`, `Ui` dispatches on backend kind (no vtable); `CellRenderer` implements grid case, new backend handles textured quads + lines. **Decided:** dispatch via `Ui::Draw*` indirection (option b from review), not virtual paint interface.

Independent (not part of UI thrust; pick up between items or when blocking): GPU-side timing (Vulkan timestamp queries → artist-visible GPU cost), pipeline state migration to `breakout`/`3d_ball_maze`/`terminal`/`wm_overlay`/`renderer.cc`/`debug_text_renderer.cc`, blend state in `.pipeline` schema, `LOG` interleave from shader worker thread, `vkDeviceWaitIdle` hitch on shader reload.

Deferred until pixel backend lands: color editor, curve editor, animation timeline.