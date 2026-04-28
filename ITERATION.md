# Iteration goals

How we want graphics developers and artists to work in this codebase. Direction only — the specifics will change.

## North star

The time between "I changed something" and "I see the result" should be measured in seconds, not in minutes or restarts. Editing a shader, swapping a texture, tweaking a constant should never require closing the app. If the loop is slow, people stop trying things — and most of what makes a game look good comes from trying things.

## Audiences

1. **Graphics developers** — write and debug shaders, render passes, pipeline state. Care about: fast shader edit/reload, visible compile errors, debugger access (RenderDoc), live tunable constants.
2. **Artists** — drop assets into the project and see them in-game. Care about: textures, meshes, audio, animation curves, color/gradient editing — all without rebuilds.

Both audiences share infrastructure; we build for the developer first because it surfaces the foundations the artist will need next.

## Phases (ordered)

### Phase 1 — close the shader loop

Make `edit .hlsl → save → see in-game` work in seconds, with no restart, no manual scripts, no asset repack. Compile errors stay on-screen and do not crash the running app. The previous version keeps running until the new one validates.

**Progress**

Done:
- Engine-owned dir watcher (`nyla/commons/dir_watcher.{h,cc}`). Polled once per `Engine::FrameBegin`. Subscribers register a path-suffix and a callback; events are dispatched in-tick.
- AssetManager dev override (`nyla/commons/asset_manager.cc`). The previous `guid < 0x100` cap on `AssetManager::Set` is gone; any guid can be overridden. `Get` checks overrides first, then the packed archive. `Subscribe(cb, user)` lets subsystems observe `Set` events.
- Dev asset bridge (`nyla/commons/dev_assets.{h,cc}`). At bootstrap it walks given roots, parses `*.meta` files for guids, watches each dir that has metas. On `.spv` modify/move-to it reads the file and calls `AssetManager::Set(guid, bytes)`. Wired into `shipgame` only so far (`shipgame/shipgame.cc` boots `DevAssets` with roots `assets`, `asset_public`).
- Shader cache (`nyla/commons/shader.{h,cc}`). `Shader::Bootstrap()` subscribes to `AssetManager`; `GetShader(guid, stage)` returns a stable `rhi_shader` handle (cached by guid+stage). On asset change, the cache calls `Rhi::ReloadShader` to swap the stored spv span — the handle stays valid. Removed the per-`CreateShader` 256MiB chunk leak: `Rhi::CreateShader` now stores the caller's spv span as-is.
- Pipeline cache (`nyla/commons/pipeline_cache.{h,cc}`). `PipelineCache::Acquire(vsGuid, psGuid, desc)` deep-copies the desc into long-lived storage and returns a stable `pipeline_cache_handle`. `Resolve(handle)` returns the current `rhi_graphics_pipeline`, called per bind. Subscribes to `AssetManager`; on a referenced guid changing, `vkDeviceWaitIdle`s, destroys the old pipeline, and rebuilds with fresh shaders. Bootstrap order is `Shader` before `PipelineCache` so the shader's spv is updated before the rebuild reads it.
- Pipeline rebuild correctness: `Rhi::CreateGraphicsPipeline` no longer overwrites the shader handle's stored spv with the post-`SpvShader::ProcessShader` buffer (which lived in scratch). Processed spv now lives in locals; the original spv on the shader handle is preserved for the next rebuild.
- Migrations: `shipgame` (world + grid pipelines), `debug_text_renderer` (foundation; used by all apps), and `renderer.cc` (foundation) now use `PipelineCache`. `breakout`, `3d_ball_maze`, `terminal`, `wm_overlay` boot `Shader` and `PipelineCache` ahead of `DebugTextRenderer`/`Renderer`.
- `DevAssets::Bootstrap` wired into every renderer-bearing app (`shipgame`, `breakout`, `3d_ball_maze`, `terminal`, `wm_overlay`) under `#if !defined(NDEBUG)` with roots `assets`, `asset_public`.
- In-app HLSL compile (`nyla/commons/dev_shaders.{h,cc}`). `DevShaders::Bootstrap(span<dev_shader_root>)` takes `(srcDir, outDir)` pairs, watches each `srcDir`, subscribes to `.hlsl` modify/move-to. On change: profile detected from `.vs.hlsl` / `.ps.hlsl` suffix, `dxc` invoked synchronously via `RunSync(cmd, alloc, &log)` (linux fork+pipe+execvp+waitpid; windows CreateProcessA + anon pipe + WaitForSingleObject), `.spv` written under `outDir`. The existing `.spv` watcher then fires reload on the next tick. dxc stdout/stderr captured into the region and `LOG`'d on failure — no python side script needed for the inner dev loop. Wired into every renderer-bearing app under `#if !defined(NDEBUG)`: foundation root `nyla/shaders → asset_public/shaders` everywhere, plus `shipgame/shaders → asset_public/shaders` for `shipgame`.
- Per-event scratch in dev paths. `dev_shaders` and `dev_assets` each split state into `persistent` + `scratch` regions. Per-event paths/log buffers and `RunSync` internal scratch live in `scratch`, reset at the top of each event. `dev_assets` resets `scratch` after the bootstrap scan as well.
- Per-guid slot reuse for reloaded asset bytes. Each `dev_asset_entry` lazy-allocates a fixed-cap (`kSpvSlotSize = 256 KiB`) slot from `dev_assets` persistent on first reload, then reads the file into that slot in place on every subsequent reload. `AssetManager::Set` gets a `byteview` over the slot. After `Set` returns, all downstream subscribers (shader cache, pipeline cache) have re-pointed at the new bytes, so the slot can be safely overwritten on the next reload. Per-edit growth is now zero; total persistent footprint is bounded by `(unique edited shaders) × kSpvSlotSize`. Files larger than the slot log an error and skip without overflowing.
- Pipeline rebuild soft-fail. `Rhi::CreateGraphicsPipeline` no longer asserts on shader interface mismatch or `vkCreateGraphicsPipelines` failure — it logs, cleans up partial state (shader modules, pipeline layout), and returns an invalid handle. `pipeline_cache::OnAssetChanged` builds the new pipeline first; if invalid, it logs and keeps the previous pipeline in place. A bytes-valid-but-pipeline-invalid spv (e.g. shader interface mismatch from the in-app dxc) now leaves the running app on the previous pipeline instead of swapping in a broken one. Initial `Acquire` still asserts on first build because there is no previous pipeline to fall back to.
- Off-thread shader compile (`nyla/commons/dev_shaders.cc`). `Bootstrap` spawns a `nyla-shadercc` worker thread that drains a mutex-protected job queue (cap 32, coalesces by srcPath). `OnHlslEvent` runs on the main thread: it resolves the root, picks the profile, fills a `compile_job` (src/out cstr paths plus dirPath/name for log) and enqueues. The worker pops one job, resets a worker-only `workerScratch`, and runs `dxc` via `RunSync`. The 50–150 ms compile no longer blocks `Engine::FrameBegin`; coalescing collapses a save-storm during an in-flight compile to at most one follow-up. Main-thread scratch is gone — only the worker uses scratch.
- Condvar wakeup for shader worker. New `platform_condvar` primitive (`nyla/commons/platform_condvar.h`, impl co-located in `platform_mutex_{linux,windows}.cc`: pthread_cond on linux, CONDITION_VARIABLE on windows). `dev_shaders` worker now `Wait`s on the queue condvar when empty; producer `Signal`s after pushing. Removes the 5 ms poll latency on idle and the wakeup hitch on save.
- Shader header dependency tracking (`nyla/commons/dev_shaders.cc`). Bootstrap walks each watched `srcDir` and indexes every `.hlsl`/`.hlsli` into a fixed-cap file table; each entry stores the includes parsed from its source. The watcher subscribes to both `.hlsl` and `.hlsli`. On `.hlsl` edit: rescan its includes, enqueue self-compile (existing behavior). On `.hlsli` edit: rescan its includes, then BFS the file table for every transitive dependent and enqueue every `.hlsl` in that set. Include scanner is line-based, matches `#include "..."` only, and resolves the include text by basename (current shader dirs are flat — when subdirs land we generalize). New `scratch` region on `dev_shaders_state` for bootstrap dir-walk and per-event dependent collection. File table cap `kFilesCap = 256`, includes per file `kIncludesPerFile = 16` — overflow logs and skips, no crash.

Effect right now: with `shipgame` running, editing any watched `.hlsl` and saving wakes the worker thread; it runs dxc and writes the new `.spv`, which fires `AssetManager::Set` on the next tick, the shader cache swaps the spv on the existing `rhi_shader` handle, and the pipeline cache rebuilds every dependent pipeline. The next frame binds the new pipeline. No restart, no side script, no asset repack, and the compile cost is off the main thread. If the rebuild fails (interface mismatch, Vulkan rejection), the previous pipeline stays bound and the error lands in the log.

Not yet done — Phase 1 deferred (both gated on "fix when it actually bites", low reward today):
- `vkDeviceWaitIdle` on every reload is a noticeable hitch; revisit if it gets in the way.
- `LOG` from the shader worker thread can interleave with main-thread `LOG` lines because `FileWriteFmt` issues multiple `FileWrite` calls per format. Single LOG calls are not atomic across threads. Fix when log scrambling actually obscures a dev-loop error — likely route is a worker-side buffered formatter that issues one `FileWrite` per line.

Deferred out of Phase 1: surfacing pipeline rebuild errors on-screen. That depends on a text/UI surface inside the running app, and the natural substrate for it is the `terminal` cell renderer (Phase 3). For now compile and rebuild errors land in stdout via `LOG` — good enough for solo dev work, not good enough as the long-term answer.

### Phase 2 — finish the dev loop without on-screen UI

Everything we can land before the cell renderer. Each item is independent of any UI substrate. Errors continue to go to stdout via `LOG` for the duration of this phase.

- Phase 1 deferred items, when they bite: `vkDeviceWaitIdle` hitch on reload, LOG interleave from the shader worker.
- ~~Shared shader headers with real dependency tracking — editing a header re-fires every dependent shader.~~ Done above.
- ~~Pipeline state (blend, depth, cull) iterable without restart — same hot-reload channel as shaders.~~ Done below.
- ~~RenderDoc capture on a hotkey.~~ Done. `RenderDocTriggerCapture()` added to `nyla/commons/renderdoc.h`; linux + windows impls (`renderdoc_linux.cc`, new `renderdoc_windows.cc`); engine binds F11 (`KeyPhysical::F11`) in `Engine::FrameBegin` to trigger a single-frame capture. Release builds compile to no-op stubs (existing pattern).
- ~~More file types on the same dir-watch + invalidation plumbing: textures, meshes, audio.~~ Done. Implementation:
    - Image decode lifted out of `asset_packer` into `nyla/commons/asset_import.{h,cc}` (`ImportTextureFromPngOrJpg`). `stb_image.h` moved from `asset_packer/` into `nyla/commons/` so both the offline packer and the in-app dev path share one decoder. `asset_packer.cc` simplified to call the shared function.
    - `dev_assets` now subscribes to `.png`/`.jpg` (decode via `asset_import`) and to `.gltf`/`.bin`/`.wav`/`.bdf` (raw passthrough). Texture decode and the raw-asset reload allocate fresh from `persistent` per reload (bounded by edits per dev session); the existing `.spv` slot-reuse path is unchanged. Reuses a new `LookupAndOpen` helper across handlers.
    - `texture_manager` subscribes to `AssetManager`. On a tracked guid changing it stashes the old `rhi_texture`/`rhi_srv` into `pendingDestroy*` slots and flips state back to `NotUploaded`. Next `Update()` call `Rhi::WaitGpuIdle`s, destroys the old GPU resources, then re-runs the upload path (which already creates fresh textures from current bytes).
    - New `Rhi::WaitGpuIdle()` API on `nyla/commons/rhi.h`, vulkan impl at the top of the file. Currently only the texture reload path calls it; the d3d12 backend (broken) does not yet implement it.
    - `mesh_manager` subscribes to `AssetManager`. On either gltf or bin guid changing it flips the slot back to `NotUploaded` so the next `Update()` re-runs the gltf parse + static buffer upload. Re-upload acquires fresh offsets in the static vertex/index buffers (the old offsets leak in the buffer until app restart — acceptable for dev). `DeclareTexture` is now skipped on reload to avoid leaking texture handle slots per edit.
    - Audio gains a clip registry: `audio_clip_handle`, `Audio::DeclareClip(guid)`, `Audio::ResolveClip(handle)`, `Audio::Play(audio_clip_handle, desc)`. `Audio::Bootstrap` subscribes to `AssetManager`; on a registered clip's bytes changing it re-runs `LoadWav` and updates the slot's `audio_clip` in place. Existing voices keep playing the old samples (still valid in `dev_assets`'s persistent region) until they finish; new `Play` calls use the fresh clip. `breakout` migrated from raw `audio_clip` storage to `audio_clip_handle`. The single-arg `Audio::Play(audio_clip)` overload stays for callers that load wavs themselves.
- Pipeline state hot-reload (mechanism). `PipelineCache::Acquire` gains an optional `stateGuid` (0 = behavior unchanged). When nonzero, the entry stores the guid; before each pipeline build the cache reads the asset bytes via `AssetManager::Get(stateGuid)` and parses them as a small text key=value file (`depth_test`, `depth_write`, `cull`, `front_face`) overriding the matching fields on the entry. `OnAssetChanged` already iterates entries — extended to also match `stateGuid`, so saving the file rebuilds the pipeline through the same path that shader changes use. `dev_assets` watches `.pipeline`; `asset_packer` adds `AssetType::Pipeline` (raw passthrough). Blend state is not in the override schema yet because the rhi pipeline desc has no blend fields exposed; add when blend lands in `rhi_graphics_pipeline_desc`.
- Pipeline state hot-reload (first call sites). `shipgame` world + grid pipelines migrated. `asset_public/shipgame_world.pipeline` and `asset_public/shipgame_grid.pipeline` carry the four-key state file; matching `.meta` siblings give them stable guids, and `shipgame.cc` passes `ID_shipgame_world_pipeline` / `ID_shipgame_grid_pipeline` to `PipelineCache::Acquire`. Saving either `.pipeline` triggers the existing rebuild path: dir watcher → `dev_assets::OnRawAssetEvent` → `AssetManager::Set` → `PipelineCache::OnAssetChanged` → `BuildPipeline` reapplies the parsed state and rebuilds. Remaining apps (breakout, 3d_ball_maze, terminal, wm_overlay) and the `renderer.cc` / `debug_text_renderer.cc` foundation pipelines still pass `stateGuid = 0` — migrate as the knobs become useful.

### Phase 3 — cell renderer (terminal project)

The `terminal` app is the first user, but the substrate lives in `nyla/commons`: a grid-of-cells renderer (text + simple attributes) that runs as a standalone terminal on Linux and Windows AND embeds into any nyla app as a custom UI surface. Because the renderer lives in `nyla/commons`, the same code path drives a real terminal on the desktop and an in-app overlay inside `shipgame`/`breakout`/etc.

Once this lands, on-screen text has somewhere to go. Everything that earlier wanted "small in-game UI" hangs off this:

- Shader compile and pipeline rebuild errors painted over the running frame instead of stdout-only.
- A live log tail / status line.
- Layout substrate for the lists, sliders, and pickers in later phases — they all bottom out in cells.

**Progress**

Done:
- Cell renderer substrate (`nyla/commons/cell_renderer.{h,cc}`). Bootstrap takes a BDF font guid, parses it into a 1024×1024 RGBA atlas (mask replicated into all four channels so the existing TextureManager/SRV path Just Works), builds a flat ASCII codepoint→glyph table, and registers the atlas via `AssetManager::Set` + `TextureManager::DeclareTexture`. Per-frame instance buffer (`CpuToGpu` vertex usage, sized `numFrames × maxCells × 16B`) is mapped at `Begin`; `PutCell`/`Text` write packed `gpu_cell` structs into the current frame's slice; `CmdFlush` issues one draw of `6 verts × cellCount instances` with the buffer bound at the frame offset. Pass constants (screen size, origin, cell size, atlas size, atlas srv index, sampler index) ride on `SetLargeDrawConstant`.
- Cell shaders (`nyla/shaders/cell_renderer.{vs,ps}.hlsl`). VS hardcodes the 6-vert quad via `SV_VertexID`, computes per-instance pixel + atlas UV from a single `uint4` ATTRIB0; PS samples the atlas and `lerp`s `bg → fg` by coverage. fg/bg ride as packed `0xAARRGGBB` uints unpacked in PS.
- New `R32G32B32A32Uint` vertex format on `rhi.h` (vulkan map, size in `GetVertexFormatSize`). Lets per-instance integer cell data avoid the bit-cast-through-float dance.
- `terminal/terminal.cc` migrated. Old `BuildFontAtlas` helper, `Renderer::Mesh(... fontAtlasTex)` demo, and unused mesh declarations gone. `CellRenderer::Begin/Text/CmdFlush` draws a few demo lines of palette samples each frame.
- Dead `nyla/commons/glyph_renderer.{h,cc}` removed (stub from before the substrate landed). Stray include in `asset_packer.cc` cleaned up.
- `lerp.h` was relying on transitive `<cmath>`; pulled `intrin.h` directly so it compiles regardless of consumer include order.

Not yet done — next chunks for Phase 3:
- ~~Pre-built shader spvs were compiled by hand once with `dxc` to seed `asset_public/shaders/cell_renderer.{vs,ps}.hlsl.spv`. Pre-existing limitation: `DevShaders::Bootstrap` only enqueues a compile on file change, so a brand-new hlsl with no spv on disk is invisible until edited. Worth adding a "compile any hlsl whose spv is missing" pass at bootstrap.~~ Done. After the dir-scan + include-rescan in `DevShaders::Bootstrap`, every indexed `.hlsl` whose `<outDir>/<relPath>.spv` is missing is enqueued via `EnqueueCompile`. The worker thread (started right after) drains the queue; the `.spv` watcher then fires reload through the existing path. Bootstrap log surfaces the missing count. New `.hlsl` files appear in-game without a manual edit-touch.
- ~~First real consumer: route `dev_shaders` / `pipeline_cache` error logs through `CellRenderer` so compile / rebuild errors paint over the running frame (the original Phase 1 deferral).~~ Done. New `nyla/commons/dev_log.{h,cc}` provides a 32-line × 128-byte mutex-protected ring buffer (`DevLog::Push` callable from any thread, `DevLog::Snapshot(alloc, maxLines)` returns most-recent-first byteviews into the caller's region). `dev_shaders` worker pushes a one-line "shader fail: <name> rc=N" on dxc failure (alongside the existing stdout LOG); `pipeline_cache` pushes "pipeline fail: <name>" when a rebuilt pipeline comes back invalid. Both use stack buffers + the explicit-buffer `StringWriteFmt` overload (the global-buf overload isn't thread-safe per its `// TODO: return to this when multithreading` note).
- ~~Embed the cell renderer into the renderer-bearing apps (`shipgame`, `breakout`, ...) as an overlay surface, not just the standalone `terminal`.~~ Done for `shipgame`, `breakout`, `3d_ball_maze`. Each app boots `DevLog` + `CellRenderer` (bdf guid `0x30B510FE27A113FB`, same as `terminal`) under `#if !defined(NDEBUG)`, and inside the existing main render pass — after `DebugTextRenderer::CmdFlush` — snapshots up to 8 lines from `DevLog`, paints them via `CellRenderer::Begin/Text/CmdFlush` at top-left in red-on-black. `wm_overlay` skipped: its rhi limits run with `numTextures=0`/`numSamplers=0` so the atlas-backed cell renderer wouldn't fit; X11 dev-overlay isn't where shader errors should surface anyway. `terminal` doesn't get an overlay either — it's already the cell renderer's primary consumer.

### Phase 4 — pleasant shader UI (on top of cell renderer)

Live-tunable constants surfaced through the cell-renderer UI so values can be scrubbed instead of recompiled. Compile and rebuild errors paint over the running frame via the same substrate (already listed as the cell renderer's first user in Phase 3).

**Progress**

Done:
- Cell renderer supports stacking draws within a frame (`nyla/commons/cell_renderer.cc`). Per-`Begin` slice byte offset auto-resets when `Rhi::GetFrameIndex()` rolls; subsequent `Begin`s in the same frame advance the slice past the previous draw's cells. `bytesPerFrame` cap unchanged; an overflow `Begin` clamps to a zero-sized draw rather than corrupting an earlier draw's bytes. Lets `DevLog` overlay and `Tunables` overlay coexist in one frame.
- Tunables subsystem (`nyla/commons/tunables.{h,cc}`). `RegisterFloat`/`RegisterInt` take `(name, ptr, step, min, max)` and append into a fixed-cap slot table. `ToggleVisible` flips on-screen state; `SelectPrev/Next` and `Decrement/Increment` operate on the selected slot. `CmdFlush(cmd, alloc, originPx)` paints a header + one-line-per-tunable list via `CellRenderer`, highlighting the selected row and formatting `float` via `%f` and `int` via `%d`. Skips drawing when not visible.
- Engine binds dev hotkeys (`nyla/commons/engine.cc`) under `#if !defined(NDEBUG)`. F1 = toggle, F2/F3 = prev/next, F4/F5 = -/+. Dev keys are still forwarded to `InputManager` so a game mapping that wants F-keys keeps working.
- `shipgame` is the first consumer. `Tunables::Bootstrap` runs in the dev block alongside `DevLog`/`CellRenderer`. Two float tunables (`camera.zoom` and `camera.metersOnScreen`) wire to the existing `game_state` fields. `metersOnScreen` was previously a `constexpr` baked into `MakeOrtho`; promoted to a runtime field so it can be scrubbed. `Tunables::CmdFlush` paints below the `DevLog` overlay each frame at `originY = 8 + 32*9` (one cell row past the 8-line dev log block).
- More register sites in `breakout` and `3d_ball_maze`. `breakout` registers `ball.radius`, `player.height`, `player.speed` against `game_state` (paddle speed promoted from a `100.f` literal to a new `playerSpeed` field). `3d_ball_maze` registers `camera.moveSpeed`, `camera.fovDeg`, `camera.lookSens`; the formerly-inlined `10.0f * frame.dt`, `90.f` FOV, and `0.1f` look sensitivity are now scrubbed at runtime. The three knobs live as file-scope `static float`s outside the dev block so release builds keep using them; only the `Tunables::Register*` calls and `CmdFlush` are dev-guarded. Both apps paint the tunables overlay at `originY = 8 + 32*9` matching `shipgame`.
- Tunable persistence (`nyla/commons/tunables.{h,cc}`). `Bootstrap(byteview persistPath)` takes a path; on entry it opens the file (if present), parses simple `name = value` lines into a fixed-cap pending list (cap = `kMaxTunables = 64`, max file size 64 KiB), and on each subsequent `RegisterFloat`/`RegisterInt` walks pending for a name match and writes the parsed value (clamped to the entry's min/max) into `*ptr`. Float-vs-int reuse the same `double` slot; identifier-style names with `.` (e.g. `camera.zoom`) use a custom `ReadNameToken` since `ParseIdentifier` wouldn't accept `.`. `Save()` overwrites the file with one `name = value\n` per registered entry (`%f` for float, `%d` for int) and `LOG`s the count + path. Engine binds F6 to `Tunables::Save` (`nyla/commons/engine.cc`, alongside F1–F5). `shipgame`/`breakout`/`3d_ball_maze` pass `<app>.tunables`; missing file is a silent no-op so first launch and release builds (which never call `Save`) just behave as before. Header line in the overlay updated to `F1 hide  F2/F3 sel  F4/F5 -/+  F6 save`.

Not yet done — next chunks for Phase 4:
- Shader push-constant register sites — needs a small bridge so HLSL-side constant-buffer slots can be exposed by name. Skipped until a concrete shader knob asks for it.
- Color and curve editors live in Phase 5; this phase only does scalar scrub.

### Phase 5 — artist tools on top

Asset browser. Color and curve editors. Profiler overlays so artists can see what their content costs on GPU and CPU. Animation timeline. These all sit on top of the cell renderer and hot-reload core.

**Progress**

Done:
- CPU profiler overlay (`nyla/commons/profiler.{h,cc}`). `Profiler::Bootstrap` allocates a fixed-cap state (64 entries, depth 8). `BeginScope(name)` / `EndScope()` push/pop a stack and record per-scope start/duration in `current[]`; depth is captured for indented display. RAII helper `profile_scope` plus `PROFILE_SCOPE("name")` macro for ergonomic instrumentation. `FrameBegin` resets the per-frame buffer and stamps frame start; `FrameEnd` closes any open scopes (defensive), measures `lastFrameUs`, and snapshots `current → display`. `CmdFlush(cmd, x, y, fps)` paints a header (`frame %.2f ms  %u fps`) plus indented per-scope rows via `CellRenderer`.
- Engine drives frame markers (`nyla/commons/engine.cc`). `Engine::FrameBegin` calls `Profiler::FrameBegin` after `DirWatcher::Tick`; `Engine::FrameEnd` calls `Profiler::FrameEnd` before `Rhi::FrameEnd`. Both null-check, so apps that don't bootstrap the profiler pay nothing. F7 hotkey toggles visibility (slotted next to the existing F1–F6 / F11 dev keys, under `#if !defined(NDEBUG)`).
- App wiring. `shipgame`, `breakout`, `3d_ball_maze` each `Profiler::Bootstrap` in the dev block alongside `DevLog`/`Tunables`/`CellRenderer`. Per-frame `Profiler::CmdFlush` paints below the `DevLog` block at `originY = 8 + 32*9`; `Tunables::CmdFlush` shifted to `8 + 32*18` to make room. `shipgame` is also the first instrumented site — `PROFILE_SCOPE("render"/"world"/"grid"/"debug_text")` wraps the main pass so the overlay shows nested CPU costs by depth.

Not yet done — Phase 5 chunks ordered by reward/effort given current solo-dev focus:

High reward, low effort (do next):
- Asset browser overlay (cell renderer). Walk `AssetManager` registered guids, paint name + size + type via `CellRenderer`, keyboard nav to inspect. Substrate is in place — `DevLog`/`Tunables`/`Profiler` already stack overlays in one frame, `assets.h` has guid→name mapping, `AssetManager` owns the rest. Mostly wiring.

High reward, medium effort:
- GPU-side timing. Vulkan timestamp queries around the same scopes (or a parallel `gpu_scope` API). Needs an RHI surface for query pools + readback; D3D12 needs the same shape. Closes the Phase 5 promise ("artists see what their content costs on GPU and CPU") now that CPU is shipped.

Medium reward — land when artist work actually starts:
- Color editor. North-star item ("color/gradient editing without rebuilds"). RGB sliders fit the cell grid as three `Tunables`-style rows; HSV picker is awkward in cells. Skip until a real consumer asks — solo dev today, no live color knobs in flight.

Questioned — likely cut from Phase 5 unless a consumer materializes:
- Curve editor. No curve-driven knob in the codebase to drive one. High effort in cell-grid UI. Defer indefinitely; reconsider only if a particle system / animation curve / tonemap LUT lands and needs scrubbing.
- Animation timeline. No animation system to time. Premature — revisit when the first animated asset actually arrives. Today this is dead weight on the plan.

## Cross-cutting foundations

These show up in every phase, build them well once:

- **A single dir-watcher owned by the engine** that dispatches changes to whichever subsystem cares.
- **Invalidation paths in every asset-holding subsystem** (shaders/pipelines, textures, meshes, audio). Each subsystem knows how to drop a stale resource and re-acquire on next use.
- **A development asset path that bypasses the packed archive** so a single file change does not require repacking. The packed archive is for shipped builds.
- **A cell-renderer UI substrate** (the `terminal` project). Not a third-party dependency — a grid of cells with text and simple attributes that doubles as a real terminal and as an in-app debug surface. Tunable constants, asset browser, profiler, curve editor all draw through this.
- **Soft failure** — a missing or broken asset shows an error (stdout for now, on-screen once the cell renderer lands) instead of crashing. The loop must keep running so the developer can fix and retry.

## Non-goals

- A full editor application. Tools live as overlays inside the running app, not as a separate program.
- Pulling in a large UI framework. We want minimal in-house, sized to the actual need.
- Supporting hot reload in shipping builds. It is a development-time feature; release builds use the packed archive and never watch the disk.
- Cross-machine collaborative editing, network sync, scene serialization formats. Out of scope.

## How we will know it is working

- Changing a shader and seeing the effect takes a few seconds, not a restart.
- A typo in a shader produces an on-screen error, not a crash, and recovers when fixed.
- An artist can drop a texture into the project and see it on a mesh without leaving the app.
- The same machinery covers shaders, textures, meshes, and audio — not four separate hand-rolled paths.
- Adding the next file type (e.g., particle system, font) is a small additive change, because the foundation already exists.
