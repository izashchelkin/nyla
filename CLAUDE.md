# nyla — agent rules

## Keep this file lean

Project moves fast; rules age fast. Each entry should be a posture or a tripwire, not a checklist. Avoid enumerating APIs, function names, or file lists — point at the wrapper file and stop. If you find yourself adding a third bullet to clarify the second, the rule is too detailed.

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
- Don't full-read large platform files (`platform_linux.cc`, `platform_windows.cc`, `rhi_vulkan.cc`, `rhi_d3d12.cc`). `grep -n` to locate, then `Read` with `offset`/`limit`.
- Apps share bootstrap boilerplate (`shipgame`, `breakout`, `3d_ball_maze`, `terminal`, `wm_overlay`). Same change across several apps → batch the Edits in one message.
- clangd noise from cross-platform indexing (`windows.h not found`, unrelated `unused-includes`) — ignore unless your edit touched that line.
- Cap build/log tails (~20 lines) unless an error demands more.
- Coredumps: `coredumpctl info` (no args = newest) — skip `list`. Mangled symbols in stack already; gdb only for regs/locals. Build-id mismatch = binary rebuilt since dump, trust frame #0 only. ASSERT in asset path? Check `assets/` for guid before patching code.
- Trust `Edit`/`Write`; don't re-read a file you just wrote.
- Spot a recurring waste (file that always needs `offset`/`limit`, generated thing that should never be read, log noise that should be filtered, redundant rule that could collapse) → suggest a concrete CLAUDE.md edit. Pose as a tripwire/posture, not a checklist. Don't apply silently — surface it so the user can accept or reject.
