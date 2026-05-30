# Better UI — Unified Design (framework extension, not replacement)

## 0. The CellRenderer problem — acknowledged and addressed

**CellRenderer is a terminal-emulator-era rendering model.** It paints monospace
glyphs into a fixed grid of 16×32 cells. It works for text-heavy tools
(asset_tool, git_ui) but it is **not the long-term UI renderer** for this
engine. The games (breakout, shipgame, maze) already bypass it entirely for
their scene rendering, using `Renderer::Mesh()` and `DebugTextRenderer::Text()`
at pixel coordinates.

The goal of this design is to ensure the **interaction model** (focus
navigation, hit-testing, widget lifecycle, scroll management) is completely
separate from the **paint backend** (CellRenderer today, something GPU-native
tomorrow). This way:

- Text-mode apps (asset_tool, git_ui) keep using CellRenderer — zero migration cost
- Future apps (and future versions of current apps) can swap in a GPU-native
  renderer without touching their widget trees or interaction logic
- The same `ui_state`, `Pump`/`Begin`/`End`, `SelectableHit`, `BeginList`/
  `ListRow`, `BeginWindow`/`EndWindow` — all work unchanged with any paint backend

**What the CellRenderer backend can't do (that a GPU-native renderer could):**
- Variable font sizes or multiple fonts in one UI
- Anti-aliased text (the cell atlas is bitmap)
- Rich text (bold/italic/underline as real rendering, not cell-flag hacks)
- Rectangles with rounded corners, gradients, alpha blending
- Sprites, icons, or images inline with text
- Proper sub-cell positioning (text kerning, fractional spacing)
- Any kind of animation (fade, slide, color interpolation)

The CellRenderer backend is **sufficient for now** — git_ui and asset_tool are
text-heavy tools where "list of colored text rows" covers 90% of the UI. When
an app needs more, the interaction model is ready for a new paint backend.

## Key insight: asset_tool and git_ui share 90% of the framework

After studying both apps side by side:

| Thing | asset_tool | git_ui |
|-------|-----------|--------|
| `ui_state` lifecycle | `Pump` → `Begin` → `End` | Same |
| Windows | `BeginWindow` / `EndWindow` (one full-screen) | Same (two side by side) |
| Lists | `BeginList` / `ListRow` / `EndList` with focus-nav scroll | Same |
| Custom row paint | `CellRenderer::Text(0, y, ...)` per visible row | Same |
| Buttons | `Ui::Button` — focusable, in focus chain | `Ui::Button` — NOT focusable (user only presses via click or global hotkey) |
| Modal | `kWindowFlagModal` + `ModalConfirmCancel` | Not used (but could be for Revert confirmation) |
| Scroll region | None needed (list handles it) | **Needed**: diff panel is a scrollable text region, not a selectable list |

They diverge in exactly two places: (1) git_ui wants buttons outside focus, (2) git_ui needs a scrollable region that isn't a selectable list.

## Design: Extend the existing framework, don't replace it

Two minimal additions to `ui_imgui.h`/`.cc`. No new headers. No structural changes. Asset_tool keeps working without modification.

### Addition 1: `kButtonFlagNoFocus` — Button that stays out of keyboard nav

```cpp
// In ui_imgui.h, add to the Button flag space (separate from kWindowFlag*):
inline constexpr uint32_t kButtonFlagNoFocus = 1u << 0;

// Button signature becomes:
auto API Button(ui_state &s, uint32_t localId, byteview label, uint32_t restingFg,
                uint32_t flags = 0) -> bool;
```

When `kButtonFlagNoFocus` is set:
- Button **skips** `s.curItems[s.curCount++] = id` — no focus chain registration
- Still paints `[ label ]` at cursor, advances cursor via `RecordItem` + `NewLine`
- Activates on **pointer press** (single-step), not release: checks `ItemHitTest` +
  `pointerPress & 1u`, returns `true` on press, consumes the press edge
- Does NOT touch `s.focusId`, `s.activeId`, or `s.activeArmedActivate` — the
  file list keeps its focus throughout the click
- `SameLine` works normally — advances cursor for subsequent widget placement
- Keyboard-navigation-inert — Arrow keys, Enter, and navActivate all ignore it

**Why press-activate, not press-then-release-activate?** The existing Button uses
"focus-then-click": first click sets focus (`s.focusId = id`), arms activation
(`s.activeArmedActivate`), then release fires if `activeId == id && activeArmedActivate`.
But a NoFocus widget can *never* be focused (not in `curItems`), so
`activeArmedActivate` is always false and clickActivate always fails.
Press-activate side-steps this: check `hit && pointerPress`, consume the edge,
return `true` on the spot.

**No press stealing**: Because buttons are at row 0 and file list rows start at
row 1, their hit rects don't overlap. Pressing a button never interferes with
a file list row's `SelectableHit` — different `y` coordinates.

**Backward compatibility**: `flags = 0` (default) preserves exact existing
behavior. Asset_tool and all existing callers compile and behave unchanged.

**Also fixes asset_tool's own UX issue**: its Save/Delete/Quit buttons sit in the
focus chain between the filter field and the list. Pressing Down from the filter
traverses 3 buttons before reaching rows. With `kButtonFlagNoFocus` they could
optionally be excluded. (Optional — asset_tool doesn't need changes.)

### Addition 2: `BeginChild` / `EndChild` — Dear ImGui–style scrollable sub-regions

This is a direct parallel to Dear ImGui's `BeginChild`/`EndChild` — a
sub-region of a window that gets its own cursor, clip stack, and optional
scroll. It replaces the narrow `ScrollRegion` proposal with a general-purpose
container widget.

```cpp
struct ui_child_desc {
    int32_t w;              // width in cells; 0 = fill remaining window width
    int32_t h;              // height in cells; 0 = fill remaining window height
    uint32_t flags;         // ui_child_flags bitfield
    uint32_t *scrollY;      // if non-null, region is scrollable; caller-owned
    uint32_t contentRows;   // total content rows; only meaningful when scrollY is set
};

inline constexpr uint32_t kChildFlagScrollable = 1u << 0;  // wheel-scrollable
inline constexpr uint32_t kChildFlagBorder      = 1u << 1;  // draw a border
inline constexpr uint32_t kChildFlagAutoSizeH   = 1u << 2;  // h fits content

auto API BeginChild(ui_state &s, uint32_t localId, const ui_child_desc &desc) -> bool;
void API EndChild(ui_state &s);
```

The `bool` return mirrors `BeginWindow` — if the child is visible (not
collapsed), the caller paints its widgets. Inside the child:
- Cursor resets to child origin
- Clip is pushed (intersected with parent)
- If `scrollY` is provided: wheel scrolls, visible range is
  `[*scrollY, *scrollY + visibleRows)`, caller iterates this range

**Dear ImGui alignment**: This matches `ImGui::BeginChild()` exactly —
ID-scoped, cursor-resetting, clip-pushing, optional scroll. The scrollable
variant replaces our `ScrollRegion` idea with a familiar and proven pattern.

The caller paints content using the child's cursor (returned by `GetCursor`),
just like painting inside a window. For scrollable children, the caller iterates
`[scrollY, scrollY + visibleRows)` and paints each visible row at `cursorX,
cursorY + rowIndex`.

---

## What changes in the framework

### `ui_imgui.h` — additions (no removals)

```
+ kButtonFlagNoFocus constant (per-Button flag, not window flag)
+ Button() gets flags parameter (default 0 = existing behavior)
+ ui_child_desc / ui_child_flags (kChildFlagScrollable, kChildFlagBorder, kChildFlagAutoSizeH)
+ BeginChild / EndChild declarations
+ Wheel state in ui_state (bool prevWheelUp, prevWheelDn + wheelEdgeUp/Dn edge booleans)
```

### `ui_imgui.cc` — additions (no renames, no restructuring)

```
+ Button(): when kButtonFlagNoFocus is set:
  - Skip curItems registration (no focus chain entry)
  - Activate on press (not focus-then-release) — check ItemHitTest +
    pointerPress, consume edge, return true. Does not touch focusId/activeId.
  - Paint, cursor advancement, hotId — all unchanged from existing logic.
+ wheelEdgeUp / wheelEdgeDn boolean fields in ui_state, set in Begin() from
  pointerPress edges (bits 3 = button 4 = wheel up, 4 = button 5 = wheel down).
  These are consumable — the first scrollable child to use them clears the edge.
+ BeginChild / EndChild implementation (~80 lines):
  - Save/restore cursor (like BeginWindow)
  - Push CellRenderer clip at (cursorX, cursorY, w, h)
  - If scrollY provided: consume wheel edges when pointer is over child rect,
    clamp scroll to valid range, expose visible range via cursor
  - If kChildFlagBorder: draw a thin border around the clip rect
  - Pop clip + restore cursor on EndChild
```

Total: ~100 new lines. Zero lines changed in existing logic stream.

---

## What git_ui becomes

```cpp
// Left window
Ui::BeginWindow(left_win)
  // File list — the only thing in the focus chain
  Ui::SetCursor(bx, by + 1);  // skip button row
  BeginList("files", ...);
    for each file:
      rr = ListRow(ls, fIdx, rowLocalId);
      if rr.visible:
        // Paint at list cursor position (rr.y reflects the absolute window row)
        CellRenderer::Text(rr.y, ...)  // color-coded status row
  EndList();

  // Buttons — NoFocus, not in keyboard nav chain, clickable only
  Ui::SetCursor(bx, by);  // repositioned to row 0
  if Ui::Button("[S]Stage", kColorButton, kButtonFlagNoFocus) → stageFile();
  Ui::SameLine(1);
  if Ui::Button("[U]Unst", kColorButton, kButtonFlagNoFocus) → unstageFile();
  Ui::SameLine(1);
  if Ui::Button("[A]StAll", kColorButton, kButtonFlagNoFocus) → stageAll();
  // ... etc
Ui::EndWindow();

// Right window — diff panel via BeginChild
Ui::BeginWindow(right_win)
  // Scrollable child filling the window body
  ui_child_desc cd{rightW, bodyRows, kChildFlagScrollable | kChildFlagBorder,
                   &diffScroll, dlineCount};
  if Ui::BeginChild(s, "diff", cd) {
    // Get child cursor origin — all painting must be relative to this.
    // The child resets cursor to its origin on Begin, pushes a clip at
    // (originX, originY, rightW, bodyRows).
    int32_t cx, cy;
    Ui::GetCursor(s, cx, cy);
    uint32_t first = diffScroll;  // mutated by BeginChild from wheel input
    uint32_t last = Min(first + (uint32_t)bodyRows, dlineCount);
    for i in [first, last):
      // Paint at absolute child-relative coords (cx, cy + row)
      CellRenderer::Text((uint32_t)cx, (uint32_t)(cy + row), dlines[i].text, DiffFg(dlines[i]), bg);
    Ui::EndChild(s);
  }
Ui::EndWindow();

// Button actions + global hotkeys dispatch after Ui::End()
if (!Ui::IsTextWidgetFocused(s)) {
    if sEdge → stageFile();
    if uEdge → unstageFile();
    if aEdge → stageAll();
    // etc — same dispatch pattern as current code
}
```

**What disappears from git_ui.cc**: `Btn()`, `Hit()`, `prevWheelUp`/`prevWheelDn`,
`diffScroll` clamp logic, the manual `placeBtn` lambda with `bxPos` tracking.
`StrColor`/`StChar` remain (they're app-level color mapping, not framework).

**Net code change**: ~120 lines removed from git_ui (manual button system +
scroll management), ~0 lines added. Framework grows ~100 lines. git_ui.cc
drops from ~700 to ~580 lines.

**Title bar**: Remains as raw `CellRenderer::Text(0, 0, ...)` outside any window.
This works because both windows start at row 1 (their `initialY = 1`). If a
future app needs title text inside window bounds, `Ui::Text()` inside a window
body handles it.

**BeginChild clamping**: `BeginChild` clamps `*scrollY` to valid range
every frame. If `contentRows` changes between frames (e.g., after a Refresh),
scroll auto-adjusts — caller doesn't need to reset it.

---

## Our framework vs. Dear ImGui — explicit mapping

Dear ImGui is the gold standard for immediate-mode UI in game engines. Our
framework (`ui_imgui`) is a miniature, cell-renderer-native take on the same
ideas. Making the mapping explicit helps design decisions:

| Dear ImGui concept | Our equivalent | Notes |
|--------------------|----------------|-------|
| `ImGui::Begin("Window")` / `End()` | `Ui::BeginWindow(s, id, desc)` / `EndWindow(s)` | Same Begin/End pattern, same id-based slot allocation |
| `ImGui::BeginChild("id", size, border, flags)` | `Ui::BeginChild(s, id, desc)` — **new** | Planned. Clip + cursor reset + optional scroll. Dear ImGui's signature: `BeginChild(str_id, size, border, flags)` |
| `ImGui::Selectable("label")` | `Ui::Selectable(s, id)` / `SelectableHit(s, id, x, y, w, h)` | Same focus chain registration + activation pattern |
| `ImGui::Button("label")` | `Ui::Button(s, id, label, fg, flags)` | Ours is cell-painted, Dear ImGui is geometry-drawn. Same interaction model |
| `ImGui::InputText("label", ...)` | `Ui::TextInput(s, id, prefix, buf, cap, len, fg)` | Keyboard-only append editor vs ImGui's full cursor/selection/clipboard |
| `ImGui::Text("format", ...)` | `Ui::Text(s, text, fg)` | Both advance cursor + paint at current position |
| `ImGui::SameLine()` | `Ui::SameLine(s, spacing)` | Identical |
| `ImGui::NewLine()` | `Ui::NewLine(s)` | Identical |
| `ImGui::SetCursorPos(x, y)` | `Ui::SetCursor(s, x, y)` | Identical |
| `ImGui::GetCursorPos()` | `Ui::GetCursor(s, &x, &y)` | Identical |
| `ImGui::PushID()`/`PopID()` | `Ui::PushId(s, id)`/`PopId(s)` | Identical hash-combine ID stack |
| `ImGui::IsItemHovered()` | `s.hotId == id` (checked by widgets internally) | Dear ImGui is explicit; ours is widget-internal |
| `ImGui::IsItemFocused()` | `s.focusId == id` (checked by widgets internally) | Same |
| `ImGui::IsItemActivated()` | Return value of widget (e.g., `Selectable().activated`) | Same return-value pattern |
| `ImGui::OpenPopup("id")` / `BeginPopup()` | `kWindowFlagModal` window painted when needed | Our modals are just windows with a flag. Dear ImGui has a separate popup stack |
| `ImGui::BeginListBox("id")` | `Ui::BeginList(s, id, desc)` / `ListRow` / `EndList` | Both: scrollable selection list. Ours is cell-grid-based, ImGui's is pixel-based |
| `ImGui::GetIO().WantCaptureKeyboard` | `Ui::IsTextWidgetFocused(s)` | Same gate for app-level hotkeys |
| `ImGui::NewFrame()` / `Render()` | `Ui::Begin(s, in, cols, rows)` / `End(s)` + `CellRenderer::CmdFlush()` | Ours combines frame setup + end + flush, ImGui separates frame + render |
| Backend (`ImDrawList`) | `CellRenderer` (current) / future GPU-native backend | Same separation: interaction model doesn't know how pixels get drawn |
| `ImGui::GetIO().MouseWheel` | wheel edges in `ui_state` — **new** | Currently missing, added by this design |
| `ImGuiWindowFlags_NoFocusOnAppearing` | `kButtonFlagNoFocus` on Button — **new** | Currently missing, added by this design |
| `ImGui::GetIO().WantTextInput` | `Ui::IsTextWidgetFocused(s)` | Both gate letter-key action dispatch |

**Key differences vs Dear ImGui**:
- **No draw-list API**: Dear ImGui builds vertex buffers (`ImDrawList`). Ours
  calls `CellRenderer::Text()` directly. A native backend would bring us closer.
- **No styling system**: Dear ImGui has `ImGuiStyle` with 50+ tunables. Ours has
  only `ui_theme { bg, focusedFg }`. Colors are caller-managed per widget.
- **No layout engine**: Dear ImGui has `SameLine` but also `SetNextWindowPos`,
  `SetNextWindowSize`, docking. Ours has only manual `SetCursor` + `SameLine` +
  `NewLine`.
- **No multi-viewport**: Dear ImGui supports rendering into multiple OS windows.
  Ours renders into one viewport (the engine's backbuffer).
- **Cell-grid positioning**: Our widgets think in `(col, row)` because they
  paint into CellRenderer's grid. Dear ImGui thinks in float pixels. This is
  the fundamental difference — and the one a native backend would eliminate.

## Implementation plan

1. Add `kButtonFlagNoFocus` + `Button(..., flags)` variant to `ui_imgui.h/.cc`
2. Add `BeginChild` / `EndChild` to `ui_imgui.h/.cc`
3. Add wheel state + edge detection to `ui_state` / `Pump` / `Begin`
4. Rewrite `git_ui/git_ui.cc` to use:
   - `Ui::Button(..., kButtonFlagNoFocus)` instead of manual `Btn()`
   - `BeginChild`/`EndChild` instead of manual wheel/clamp/diff loop
   - Remove `Btn()`, `Hit()`, `prevWheelUp/Dn`, `diffScroll` clamp logic
5. Verify asset_tool still builds and works unchanged
6. Verify git_ui builds and all features work (file nav, stage/unstage, diff scroll, resize)

## Open questions

**Should BeginChild handle PageUp/PageDown?** Yes — scrollable children read from
`ui_frame_input.navPgUp/navPgDn` when available. Currently those are always
false (KeyPhysical lacks PageUp/Down). When the platform layer adds them,
BeginChild automatically benefits.

**Should Button respond to Enter when focused-but-NoFocus?** No — if it's not in the focus chain it can never be focused. Click only.

**What about the title bar?** It stays as raw `CellRenderer::Text(0, 0, ...)` — it's outside any window and doesn't need framework involvement. Same as current code.

**Does asset_tool need the kWindowFlagReposition flag we already added?** No — asset_tool's window fills the viewport and uses `initialX=0, initialY=0`, which is always correct. The flag is unused by asset_tool but harmless.

---

## 2. Future: Game engine UI (not targeted yet — design notes)

> **Status: deferred.** The games (breakout, shipgame, 3d_ball_maze) have no game UI today.
> This section exists to ensure the text-mode extensions don't close doors, and to
> document what a unified interaction model would look like when game UI is built.

### What the games currently have

The three game apps — breakout, shipgame, 3d_ball_maze — currently have **no game UI at all**. They render frames via `Renderer::Mesh()` (3D/2D mesh pipeline), with only debug overlays:

| Debug use | Renderer | When |
|-----------|----------|------|
| FPS counter | `DebugTextRenderer::Fmt(500, 10, ...)` | Always (pixel coords) |
| DevLog lines | `CellRenderer::Text(0, i, ...)` | `NDEBUG` only (cell coords) |
| Profiler overlay | `Profiler::CmdFlush(...)` | `NDEBUG` only |
| Tunables UI | `Tunables::CmdFlush(...)` | `NDEBUG` only |

### What they'd eventually need

**Breakout**: Score display, lives remaining, level indicator, pause menu, "Game Over" / "You Win" screen, title screen with "Press Enter to Start."

**Shipgame**: Minimap in corner, ship health/energy bars, weapon indicator, planet labels in world space, pause menu, galaxy map, inventory/powerup screen, coordinates display.

**3d_ball_maze**: Compass/position readout, timer, level transitions, "Maze Complete" screen, reset button, maybe a minimap.

### Why they can't use ui_imgui

The games render into a **3D/2D world-space render pass** (`Renderer::Mesh()`), not into a cell grid. The UI framework's assumptions are all wrong for games:

| ui_imgui assumption | Game reality |
|---------------------|--------------|
| Renders via `CellRenderer::Text()` at cell coords | Renders via `Renderer::Mesh()` and `DebugTextRenderer::Text()` at pixel coords |
| Solid `theme.bg` rectangle behind every widget (occludes scene) | Transparent overlay — must not occlude the scene |
| Viewport is `cols × rows` text cells | Viewport is pixel-sized (`width × height`) |
| Scroll via `BeginList` with focus-nav | No scroll needed — HUDs are fixed-position, menus are full-screen overlays |
| `BeginWindow` / `EndWindow` with title bars and drag | No windows — fixed HUD positions, centered menus |
| Keyboard-only focus navigation | Gamepad-friendly navigation (d-pad, stick movement, A/B buttons) |

### DebugTextRenderer — the natural game UI backend

`DebugTextRenderer` already exists and is used by all three games for FPS display. It has:
- **Pixel-positioned text**: `Text(x, y, text)` — pixel coordinates, not cell grid
- **GPU-driven**: batched draw calls via pipeline cache, renders after the main scene pass
- **No background fill**: text floats over whatever was already rendered — transparent by default
- **Fixed-function**: no widget abstraction, just "draw this string at this pixel position"

A game UI framework would wrap `DebugTextRenderer`, providing:
- `Label(x, y, text, color)` — fixed text
- `Button(x, y, w, h, text)` — clickable with hover highlight
- `Bar(x, y, w, h, fill_pct, color)` — health/energy/progress bar
- `Menu(items[])` — vertical list of selectable items (keyboard + gamepad navigation)
- All rendered as overlays after the scene pass

### The natural split: multiple UI backends, one shared interaction model

```
                    ┌──────────────────────┐
                    │   ui_frame_input     │  (Pump: keyboard nav + pointer)
                    │   ui_state           │  (Begin/End lifecycle, focus, hit-test)
                    └──────┬───────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
    ┌─────────▼─────┐ ┌───▼──────┐ ┌───▼──────────┐
    │  ui_imgui     │ │ ui_game  │ │ ui_native     │
    │  (cell grid)  │ │ (pixels) │ │ (GPU-native,  │
    │               │ │          │ │  post-CellRen) │
    ├───────────────┤ ├──────────┤ ├───────────────┤
    │ Backend:      │ │ Backend: │ │ Backend:      │
    │  CellRenderer │ │  DebugTxt│ │  TBD: GPU mesh│
    │               │ │  Renderer│ │  + atlas font │
    ├───────────────┤ ├──────────┤ ├───────────────┤
    │ Used by:      │ │ Used by: │ │ Used by:      │
    │  asset_tool   │ │ (future) │ │  git_ui v2    │
    │  git_ui v1    │ │ breakout │ │  asset_tool v2 │
    │               │ │ shipgame │ │  terminal v2  │
    │               │ │ maze     │ │               │
    └───────────────┘ └──────────┘ └───────────────┘
```

The CellRenderer module (`cell_renderer.h/.cc`) is a paint backend — nothing more.
The UI framework (`ui_imgui`) is an immediate-mode retained-state widget system.
They are already separate. Swapping CellRenderer for a GPU-native renderer means:

1. Write a new paint backend (`ui_native.h/.cc`) that renders widgets via
   `Renderer::Mesh()` with a font-atlas texture (same approach as
   `DebugTextRenderer` but with proper text shaping, variable sizes, and
   rectangle primitives)
2. `ui_imgui`'s `Text()`, `Button()`, `BeginWindow()` body fill, and
   `BeginChild` clip all call the new backend instead of CellRenderer
3. Widget registration, focus nav, hit-testing, scroll management — all unchanged

The `ui_state` struct and `Pump`/`Begin`/`End` lifecycle are completely
renderer-agnostic. They manage focus IDs, pointer state, scroll offsets,
and widget registration. They don't know what `Text()` does internally.

**Migration path**: `ui_imgui` gets a conditional flag at bootstrap time
(`Ui::Bootstrap(ui_backend::CellRenderer)` or `Ui::Bootstrap(ui_backend::Native)`).
Existing apps don't change. New apps opt into the native backend. Same API,
same widget types, different pixel output.

### What the games don't need (that the text UI has)

- No window management — titles, dragging, clipping, z-order
- No `SetCursor` / `NewLine` / `SameLine` flow layout
- No list scrolling with focus-based auto-scroll
- No modal dialogs (menu screens are full-screen overlays, not popups)
- No `TextInput` (no text entry in games — button/A-key to confirm)
- No `PushClip` / `PopClip` (games render transparent overlays)

### Per-game UI needs (when built)

**Breakout** — nearest to needing UI:
- Score display: top-center, large text, updates each brick hit
- Lives counter: top-right, `❤ × 3` or similar
- Level indicator: top-left, `Level 3`
- Pause overlay: semi-transparent background + centered menu (Resume / Restart / Quit)
- Game Over screen: "Game Over — Score: 1234" + "Press Enter to restart"
- Title screen: "Breakout" title + "Press Enter to start" + high score

**Shipgame** — most complex:
- HUD: ship health bar, energy bar, weapon icon (top-left corner)
- Minimap: small radar showing planets and ship (bottom-right corner, 100×100 px)
- Targeting reticle / lead indicator on nearest planet
- Planet labels: planet name floating above each planet in world-space projection
- Pause menu: Resume / Galaxy Map / Inventory / Quit
- Galaxy map: full-screen overlay showing all systems, jump fuel range circle
- Damage flash: screen-edge red vignette when hit

**3d_ball_maze** — simplest:
- Timer: top-right, elapsed time
- Position/compass: maybe top-left, "Facing: N" or coordinates
- Reset button: bottom-center, "[R] Reset"
- Win screen: "Maze Complete! Time: 12.3s" + "Press R to restart"

### What can be shared between text UI and game UI

- `Pump()` → `ui_frame_input` (keyboard repeat, pointer edges)
- `Begin()` / `End()` — frame lifecycle, focus nav over a flat item chain
- `Selectable()` / `SelectableHit()` — focus registration, click actuation
- Pointer hit-test infrastructure
- The concept of "flat focus chain with modal gating"

The game UI would share these core abstractions (possibly by including `ui_imgui.h` and using `ui_state` + `Pump`/`Begin`/`End`), but replace the paint layer with `DebugTextRenderer` calls at pixel coordinates instead of `CellRenderer` calls at cell coordinates.

### Sketch of future `ui_game` API (not implementing now)

```cpp
// ui_game.h — pixel-overlay UI for games (uses DebugTextRenderer, not CellRenderer)

namespace UiGame {

// Reuses ui_state, ui_frame_input, Pump(), Begin(), End() from ui_imgui.
// Only the paint primitives differ.

// Fixed-position text label (HUD element, score, etc.)
void Label(ui_state &s, uint32_t localId, int32_t pxX, int32_t pxY, byteview text, uint32_t color);

// Clickable button at pixel coords. Returns true on click this frame.
// Uses DebugTextRenderer for paint — draws border + text at (pxX, pxY).
auto Button(ui_state &s, uint32_t localId, int32_t pxX, int32_t pxY, int32_t pxW, int32_t pxH,
            byteview text, uint32_t fg, uint32_t bg, uint32_t flags) -> bool;

// Progress/health/energy bar (filled rectangle + border + text overlay)
void Bar(ui_state &s, int32_t pxX, int32_t pxY, int32_t pxW, int32_t pxH,
         float fillPct, uint32_t fillColor, uint32_t bgColor);

// Vertical menu of selectable items (d-pad/arrow-nav, A/Enter to confirm).
// Paints a column of text items at (pxX, pxY), with focus highlight.
// Returns index of activated item, or UINT32_MAX if none.
struct menu_item { byteview text; uint32_t fg; uint32_t bgFocused; };
auto Menu(ui_state &s, uint32_t localId, int32_t pxX, int32_t pxY,
          span<const menu_item> items) -> uint32_t;

} // namespace UiGame
```

Game apps would use the same `Pump`/`Begin`/`End` lifecycle:

```cpp
// In breakout UserMain():
ui_frame_input in = Ui::Pump(s, frame, 0); // no pgStep for menu
in.pointerX = frame.pointerX;  // pixel coords, not cell coords
in.pointerY = frame.pointerY;

Ui::Begin(s, in, backbufferWidth, backbufferHeight);

if (gameState == GameState::Paused) {
    uint32_t choice = UiGame::Menu(s, 1, centerX, centerY, pauseItems);
    if (choice == 0) gameState = GameState::Playing;  // Resume
    if (choice == 1) Engine::RequestExit();             // Quit
} else {
    // HUD
    UiGame::Label(s, 2, 20, 20, scoreText, kFg);
    UiGame::Label(s, 3, 20, 50, livesText, kFg);
    UiGame::Bar(s, 20, 80, 200, 16, healthPct, kHealthGreen, kBarBg);
}

Ui::End(s);
```

### What this means for the current ui_imgui extensions

The `kButtonFlagNoFocus` and `BeginChild` additions to `ui_imgui` are correct and sufficient
for text-mode apps. They don't obstruct future game UI — `ui_game` would share `ui_state` +
`Pump`/`Begin`/`End` from the same module, adding only a `DebugTextRenderer` paint layer.

**No scope creep.** The current plan stays focused on text-mode extensions.
Game UI is documented here so the design is explicit about what's deferred and why
it's compatible.
