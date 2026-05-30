# Git UI MVP — Progress Log

## Implemented

### Framework additions (`nyla/commons/ui_imgui.h/.cc`)
- `kButtonFlagNoFocus` — Button flag that skips focus chain registration, activates on press
- `kWindowFlagReposition` — Window flag that re-applies `initialX/Y` every frame
- `BeginChild` / `EndChild` — scrollable, bordered sub-region with cursor+clip management
- `kChildFlagScrollable`, `kChildFlagBorder`, `kChildFlagAutoSizeH` — child flags
- Wheel edge detection (`wheelEdgeUp`/`wheelEdgeDn` in `ui_state`, latched in `Begin()`)
- Button signature extended with `flags` parameter (default 0 = backward compatible)

### Git UI (`git_ui/git_ui.cc`)
- Parses `git status --porcelain=v1` into color-coded file rows (M/A/D/?/R/C)
- Side-by-side layout: left panel (buttons + file list), right panel (diff)
- Arrow keys navigate files only — buttons use `kButtonFlagNoFocus`, stay out of focus chain
- Scrollable diff via `BeginChild` with border, mouse wheel scroll, scroll position indicator
- Parsed diff output: stripped boilerplate, structured into Header/Separator/Hunk/Add/Del/Ctx lines
- Actions: Stage/Unstage/StageAll/UnstageAll/Revert/Diff-refresh/Refresh/Quit
- Keyboard shortcuts: S/U/A/Z/R/D/F5/Q with edge detection
- Resize-aware via `kWindowFlagReposition`
- Help text in button bar, context-sensitive right panel ("Select a file..." / "Loading diff...")

### Design documents
- `git-ui-design.md` — full architecture analysis, Dear ImGui mapping, game UI future
- `git-ui-progress.md` — this file (implementation record)

## Next steps — from `git-ui-design.md`

1. **GPU-native text renderer** — replace CellRenderer paint backend with a debug-text-renderer-style GPU pipeline using font-atlas textures, proper anti-aliasing, variable sizes, rectangles with borders, and alpha blending. Interaction model unchanged.

2. **Game UI module** (`ui_game.h/.cc`) — pixel-overlay UI for breakout/shipgame/maze, reusing `ui_state` + `Pump`/`Begin`/`End` with `DebugTextRenderer` paint backend. Widgets: Label, Button, Bar, Menu.

3. **Action confirmation** — Revert should prompt "Are you sure?" via modal window

4. **Commit workflow** — commit message input, staging area with partial stage/unstage
