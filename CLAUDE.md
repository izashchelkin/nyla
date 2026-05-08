# nyla — agent rules

## Keep this file lean

Project moves fast; rules age fast. Each entry should be a posture or a tripwire, not a checklist. Avoid enumerating APIs, function names, or file lists — point at the wrapper file and stop. If you find yourself adding a third bullet to clarify the second, the rule is too detailed.

## Cleanup tripwires

Recurring slop patterns and the methodology to fix them live in the `cleanup` skill (`.claude/skills/cleanup/SKILL.md`). When you spot a tripwire (duplicated helper, manual span/copy, naming drift, half-finished refactor) in code you're already touching, invoke the skill and fix it in the same pass.

## Token-econ tripwires

Repo read-pattern rules and the survey methodology for finding new ones live in the `token-econ` skill (`.claude/skills/token-econ/SKILL.md`). Static rules live in the **Token economy** section below. Invoke the skill when you want to scan for fresh waste sources or propose new tripwires.

## Working with `ITERATION.md`

Living plan, not a changelog. Read it for current focus before starting any non-trivial task. Three sections do real work: **Posture** (rules that hold across all work), **What works today** (capability inventory), **What's pending** (priority queue).

Editing rules:

- When a pending item ships, **move it** into "What works today" as a one-line bullet with entry-point file. Do not keep a "Done" log — `git log` is authoritative.
- "What works today" stays terse: one line per capability, file path, no narration. Trim or merge bullets when a feature subsumes another.
- "What's pending" stays at 5-7 items. Anything beyond that is noise — drop it or fold into a parent item. Top of list = next thing.
- Each pending item gets a **Why** (the motivation) and a **How** (concrete entry point or sketch). Items without both rot fastest.
- Posture is for *recurring* rules. New rule earns a place only when the same instance appears in three or more places. Edit existing rules in place; don't append a variant per case.
- No references to specific commits, PRs, dates, or completed work. The doc describes the state, not the journey.
- When the doc drifts toward narrative paragraphs or grows past ~80 lines, that's a signal to compact — same posture as code review on prose.

## Project shape

Durable facts. Phase-specific work lives in `ITERATION.md` when present — read it for current focus.

- Engine (`nyla/`, MIT-licensed) plus apps at repo root (`shipgame`, `breakout`, `terminal`, ...). Games built on top will be closed-source.
- Target hardware: x86 PC, DX12-class GPU.
- **Windows first; Linux must always work.** Some apps are Linux-only (`wm`, `wm_overlay`).
- RHI is **Vulkan first, D3D12 next** (D3D12 backend currently broken). New `rhi.h` abstractions must keep D3D12 in scope — when unsure, design for D3D12 and reach parity from Vulkan via extensions.
- `nyla/commons` exists for control and substitutability; bloated C++ libs are out. Header-only stdlib pieces (`<concepts>`, `<cstdint>`, ...) are fine. Pragmatic stdlib use is allowed where a custom impl isn't worth the effort (e.g. `printf` for floats inside `fmt.cc`).
- Solo project today; external artists, then developers, will join over time. Write for a future contributor, not just yourself.

## Layout

- Subdirs flat. No nested folders inside `nyla/commons/` etc. All `.cc`/`.h` siblings.
- Apps live at repo root (`shipgame/`, `breakout/`, `wm/`, ...). Not under `nyla/`.
- New assets default to `asset_public/`. Use `assets/` only when the user explicitly asks (private/closed-source assets land there).

## Generated CMake — DO NOT EDIT BY HAND

Each subdir has `CMakeListsGenerated.txt` produced by `gencmake.ps1`. Add/remove/rename `.cc`/`.h` → run the script, never hand-edit:

```
pwsh ./gencmake.ps1 <dir>
```

Suffix routing (auto):
- `_windows.cc/.h` → `if (WIN32)` branch
- `_linux.cc/.h` → `else()` branch
- `_vulkan.cc/.h` → `if (Vulkan_FOUND)` branch
- `_d3d12.cc/.h` → `if (D3D12_FOUND)` branch
- everything else → common section

Hand-written wiring (`target_link_libraries`, `find_package`, options) lives in `CMakeLists.txt` next to the generated file, which `include()`'s it.

## Prefer nyla/commons headers over std

Wrapper exists in `nyla/commons` → use it. Wrappers keep platform/compiler quirks in one place. Bypass = drift.

- math/intrinsics → `nyla/commons/intrin.h` over `<cmath>`/`<atomic>`/`<bit>`/`<cstdlib>`.
- performance/inlining → Use `INLINE` macro from `nyla/commons/macros.h` for hot-path helpers instead of raw `inline` or `__attribute__((always_inline))`. Note: `INLINE` is compatible with both free and member functions.
- pi → `math::pi` from `nyla/commons/math.h` (not `M_PI`).
- mem ops → `nyla/commons/mem.h` over `<cstring>`.
- spans/bytes → `nyla/commons/span.h` (`byteview`, `span<T>`) over `std::span`.
- containers → `nyla/commons/inline_vec.h`, `inline_string.h`, `array.h`, `handle_pool.h` over `<vector>`/`<string>`/`<array>`.
- threads/sync → `nyla/commons/platform_thread.h`, `platform_mutex.h` over `<thread>`/`<mutex>`.
- alloc → `nyla/commons/region_alloc.h` over `new`/`malloc`.
- file I/O → `nyla/commons/file.h` / `file_utils.h` over `<fstream>`/`<cstdio>`.
- fmt/log → `nyla/commons/fmt.h` (`LOG`, `Fmt`) over `<iostream>`/`printf`.

No wrapper yet → ask before reaching std. Usually belongs in `intrin.h` or a sibling.

## Token economy

- Don't read `CMakeListsGenerated.txt`. Re-run `gencmake.ps1`; `grep` to verify.
- Don't full-read large platform / vendored / generated files (`platform_linux.cc`, `platform_windows.cc`, `rhi_vulkan.cc`, `rhi_d3d12.cc`, `stb_image.h`, `spv_shader_enums.h`, `renderdoc_app.h`). `grep -n` to locate, then `Read` with `offset`/`limit`. Don't edit vendored/generated.
- Apps share bootstrap boilerplate (`shipgame`, `breakout`, `3d_ball_maze`, `terminal`, `wm_overlay`). Same change across several apps → batch the Edits in one message.
- clangd noise from cross-platform indexing (`windows.h not found`, unrelated `unused-includes`) — ignore unless your edit touched that line.
- Cap build/log tails (~20 lines) unless an error demands more.
- Coredumps: `coredumpctl info` (no args = newest) — skip `list`. Mangled symbols in stack already; gdb only for regs/locals. Build-id mismatch = binary rebuilt since dump, trust frame #0 only. ASSERT in asset path? Check `assets/` for guid before patching code.
- Trust `Edit`/`Write`; don't re-read a file you just wrote.
- Spot a recurring waste (file that always needs `offset`/`limit`, generated thing that should never be read, log noise that should be filtered, redundant rule that could collapse) → suggest a concrete CLAUDE.md edit. Pose as a tripwire/posture, not a checklist. Don't apply silently — surface it so the user can accept or reject.
