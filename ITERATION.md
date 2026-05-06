# Iteration goals

How we want graphics developers and artists to work in this codebase. Direction only — the specifics will change.

## North star

The time between "I changed something" and "I see the result" should be measured in seconds, not in minutes or restarts. Editing a shader, swapping a texture, tweaking a constant should never require closing the app. If the loop is slow, people stop trying things — and most of what makes a game look good comes from trying things.

## Audiences

1. **Graphics developers** — write and debug shaders, render passes, pipeline state. Care about: fast shader edit/reload, visible compile errors, debugger access (RenderDoc), live tunable constants.
2. **Artists** — drop assets into the project and see them in-game. Care about: textures, meshes, audio, animation curves, color/gradient editing — all without rebuilds.

Both audiences share infrastructure; we build for the developer first because it surfaces the foundations the artist will need next.

## Posture

These hold across all work, not just the items below.

- **Cleanup runs ahead of new surface area.** Tripwires and methodology live in the `cleanup` skill (`.claude/skills/cleanup/SKILL.md`). Invoke it when starting a cleanup pass; auto-applies during ordinary editing when a tripwire is hit.
- **Soft fail.** A missing or broken asset shows an error (stdout, or `DevLog` overlay) and keeps the loop running. Never crash the dev loop.
- **Hot reload is dev-only.** Release builds use the packed archive, never watch the disk. Guard call sites with `#if !defined(NDEBUG)`.
- **Single focus chain in UI.** At most one widget owns focus per frame (`ui_state.focusId`). Every focusable widget registers in the chain on call; keyboard `Ui::Pump` cycles through it, mouse click shifts focus to the clicked widget, modal scope confines the chain to the modal's contents. The topmost window under the pointer (prev-frame z-order) absorbs background hits so widgets behind don't fire. Activation fires on `navActivate` while focused (keyboard) or click-release-while-over (mouse). Plain `Ui::Text` and other non-interactive paint do NOT enter the chain.

## What works today

Single rolling inventory. One line per capability, with entry-point file. When a pending item ships, move it here and trim the prose.

- **Engine-owned dir watcher** — polled per `FrameBegin`, suffix-keyed subscribers (`dir_watcher.cc`).
- **Dev asset bridge** — walks `assets`/`asset_public`, parses `.meta` for guids, dispatches file changes to `AssetManager::Set` (`dev_assets.cc`).
- **Shader hot reload** — edit `.hlsl`/`.hlsli`, off-thread dxc compile (header dependency tracked, missing `.spv`s auto-built at bootstrap), spv → asset manager → shader cache → pipeline cache rebuild, next frame uses new pipeline (`dev_shaders.cc`, `shader.cc`, `pipeline_cache.cc`).
- **Pipeline rebuild soft-fail** — interface mismatch or Vulkan rejection logs and keeps the previous pipeline bound (`pipeline_cache.cc`).
- **Texture / mesh / audio invalidation** — `dev_assets` watches `.png`/`.jpg`/`.gltf`/`.bin`/`.wav`/`.bdf`; subsystems re-upload on `AssetManager` change.
- **Pipeline state hot-reload** — `.pipeline` text key=value files watched, parsed into `rhi_graphics_pipeline_desc` overrides on rebuild. First sites: `shipgame` world + grid.
- **RenderDoc capture** — F11 in dev builds (`renderdoc.h`).
- **Cell renderer substrate** — BDF font → atlas → per-frame instance buffer; stackable in one frame; embedded as overlay in `shipgame` / `breakout` / `3d_ball_maze`; primary consumer is `terminal` (`cell_renderer.cc`).
- **DevLog ring buffer** — compile/rebuild errors painted on-screen via cell renderer (`dev_log.cc`).
- **Tunables** — `RegisterFloat`/`RegisterInt`, F1 toggle / F2-F3 select / F4-F5 ±/F6 save, persisted to per-app `.tunables` file (`tunables.cc`).
- **CPU profiler overlay** — `PROFILE_SCOPE` macro, indented per-scope display, F7 toggle. `shipgame` instrumented (`profiler.cc`).
- **Imgui-style UI framework** — id-stack (uint32 + `byteview` overload via FNV-1a), single focus chain (one focused widget per frame, keyboard cycling via `Ui::Pump` Up/Down/Home/End across widgets in registration order; click-press shifts focus + arms `activeId`, click-release-while-over activates — `Button` requires the press to land on an already-focused widget, so first click focuses, second click presses; `SelectableHit` activates on any release-while-over), layout cursor, `Text` / `TextInput` / `Selectable` / `SelectableHit` / `Button` / single-slot modal under `nyla::Ui` (`ui_imgui.cc`). `Ui::BootstrapInput` claims `input_id::Custom1..Custom6` (Up/Down/Enter/Esc/Y/N); `Ui::Pump(ui_state, engine_frame, pgStep)` produces `ui_frame_input` with edge detect + nav key repeat (caller fills `pointerX/Y/Buttons/Press/Release` in widget coords before `Begin`). Theme palette (`ui_state.theme`, default `kDefaultUiTheme`) drives widget bg + focused fg; `Button` inverts bg on focus so default-theme contrast holds regardless of `restingFg`. Semantic row colors stay caller-side. Backend-agnostic; first paint backend is `CellRenderer`, pixel-precise backend follows when widgets need it. Engine pumps utf8 input through `engine_frame.textChars` from xkb on Linux / `WM_CHAR` on Windows. First consumer: `asset_tool/`.
- **Pointer plumbing** — `engine_frame` carries window-pixel `pointerX/Y`, `pointerDx/Dy`, `pointerButtons` (current state) plus `pointerPress` / `pointerRelease` edges this frame. X11 subscribes to `XCB_EVENT_MASK_POINTER_MOTION` + button events; Win32 handles `WM_MOUSEMOVE` / `WM_{L,M,R}BUTTON{DOWN,UP}` with X11-parity codes (1=L 2=M 3=R). `Ui::ItemHitTest` + `Ui::SelectableHit` consume the converted (widget-coord) pointer; modal flag absorbs background hits (`engine.cc`, `platform_linux.cc`, `platform_windows.cc`, `ui_imgui.cc`).
- **Engine in-app exit** — `Engine::RequestExit()` sets the same flag the platform Quit event sets, so any in-app caller can drive shutdown (`engine.cc`).
- **Asset archive manager** — `asset_tool/` opens an archive (`argv[1]`, default `assets.bin`), lists guid + size + type + alias (sidecar `.meta` walked from `asset_public/`/`assets/`), toolbar row above list with Save/Delete/Quit buttons, `X` marks delete, `S` saves filtered archive in place, filter / add `TextInput`s. Add reads file, sniffs type, mints `.meta` at save. Save confirm = `kWindowFlagModal` window (`asset_tool.cc`).
- **Draggable windows** — `Ui::BeginWindow(localId, ui_window_desc)` / `Ui::EndWindow` paints a title bar + body, pushes a `CellRenderer` clip, saves/restores cursor. Title-bar drag, click-to-front, top-most-only hit-test gating via prev-frame z-order. Slot pool keeps positions stable; first allocation registers `ui.win.<hex>.{x,y}` int tunables so positions survive the per-app `.tunables` file (`ui_imgui.cc`, `cell_renderer.cc` clip stack).
- **Modal-as-window + cross-window focus** — `kWindowFlagModal` in `ui_window_desc.flags` makes the window the active modal: forced top z each frame, background widgets gated (nav/click/text suppressed via `s.modalActive && !s.inModalScope`), focus cycling restricted to widgets registered inside the modal (slot tracks `prevItemsBegin/End`). Y/N edges flow through `Ui::ModalConfirmCancel(s)` (no localId). Freshly opened windows snap focus to first registered child via `slot.openedJustNow`. Non-modal cross-window cycling already free from the flat `prevItems` chain. Replaces the old `OpenModal`/`IsModalOpen`/`CloseModal`/`ModalConfirmCancel(localId)` API (`ui_imgui.cc`).

## What's pending (priority order)

Top of list = next thing to work on. Trim to the top 5-7. Each item: rule/feature, **Why**, **How**, plus **Decided** / **Open** lines where ambiguity must survive a context drop.

Active thrust: **UI framework — Imgui-style GUI for in-app overlays + standalone tools**. Remaining path: list scope primitive + wrap asset_tool main view, pixel backend.

1. **Cleanup pass over `nyla/commons`** — invoke the `cleanup` skill; concrete pending instances live there. **Why:** slop accumulates faster than it gets fixed. **How:** `/cleanup`, sweep one tripwire at a time.

2. **List scope + wrap asset_tool main in a window** — `BeginList`/`EndList` primitive (scroll + clip + visible-only id reg, caller-supplied row id stable across reorder). **Why:** `asset_tool` main view (toolbar + list) still paints outside any window with hand-rolled `DrawListRows` + `EnsureVisible`; window-vs-non-window split keeps the special-case scrolling code alive. **How:** wrap `asset_tool` main in `BeginWindow`, replace `DrawListRows` + `EnsureVisible` with list primitive (visible rows through `SelectableHit`, off-screen through paint-free `Selectable`). Page-key nav (`PgUp`/`PgDn`/`Home`/`End`) reserved in `ui_frame_input` but inert until `KeyPhysical` exposes those keys.

3. **Pixel-precise paint backend** — second backend alongside `CellRenderer` for non-grid widgets (icons, sliders, color swatches, window chrome). **Why:** cell grid caps what GUI can express; pointer-precise hit areas + chrome outgrow it. **How:** widgets call `Ui::DrawText` / `Ui::DrawRect`, `Ui` dispatches on backend kind (no vtable); `CellRenderer` implements grid case, new backend handles textured quads + lines. **Decided:** dispatch via `Ui::Draw*` indirection (option b from review), not virtual paint interface.

Independent (not part of UI thrust; pick up between items or when blocking): GPU-side timing (Vulkan timestamp queries → artist-visible GPU cost), pipeline state migration to `breakout`/`3d_ball_maze`/`terminal`/`wm_overlay`/`renderer.cc`/`debug_text_renderer.cc`, blend state in `.pipeline` schema, `LOG` interleave from shader worker thread, `vkDeviceWaitIdle` hitch on shader reload.

Deferred until UI thrust lands (2)+(3): color editor, curve editor, animation timeline.

## Non-goals

- A full editor application. Game-time tools live as in-app overlays; standalone tools (`asset_tool/`) stay scoped to ops the running app can't do (archive editing, build-time inspection).
- Pulling in a large UI framework. Minimal in-house, sized to the actual need.
- Hot reload in shipping builds.
- Cross-machine collaborative editing, network sync, scene serialization formats.