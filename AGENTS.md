# Nyla — AI Context File

> **AI quickstart**: C++23, no STL, no exceptions, flat `.cc` files, trailing return types,
> `#pragma once`, `span`/`inline_vec`/`array` over std containers, `ASSERT` over exceptions.
> Build: `cmake --preset linux-debug && cmake --build build/linux-debug --target wm`.
> Test: `./scripts/test_wm.sh`. Test on live hardware too — not all bugs reproduce in Xvfb.
> Be honest about changes. Ask before implementing. Call out hallucinations.

C++23 cross-platform application/game framework with custom engine core and multiple desktop apps. Cross-compiles Linux (clang++) + Windows (MSVC/clang-cl).

## Build

```
# Linux debug (default for development)
cmake --preset linux-debug
cmake --build build/linux-debug

# Linux release
cmake --preset linux-release
cmake --build build/linux-release

# Single target
cmake --build build/linux-debug --target wm

# Clang-format all sources
cmake --build build/linux-debug --target format

# Compile shaders (HLSL → SPIR-V + DXIL)
python scripts/shaders.py
```

Formatters: `.clang-format`, `.clang-tidy` (modernize/bugprone/performance checks enabled).

## Architecture

```
nyla/commons/      ← Core engine (~140 files). Everything depends on this.
  ↑
  ├── wm/          ← X11 tiling window manager
  ├── wm_overlay/  ← Status bar rendered over WM
  ├── terminal/    ← Terminal emulator
  ├── breakout/    ← Breakout game
  ├── shipgame/    ← Spaceship game
  ├── 3d_ball_maze/ ← 3D maze game
  ├── screen_inhibitor/
  ├── asset_packer/
  ├── asset_tool/
  └── psf2_glsl/
```

### Key patterns in commons

- **`region_alloc`** — `{ at, begin, end, commitedEnd }`. Arena allocator. All frame data allocated here, valid for one frame. See `engine.h`: `Engine::FrameBegin(alloc)` returns `engine_frame` with frame-scoped data.
- **`handle { gen, index }`** — Resource handles with generation-based invalidation. `is_handle` concept. Compare by value, never dereference.
- **`span<T>`** — `{ T* data, uint64_t size }`. Non-owning view. `byteview = span<const uint8_t>`.
- **`inline_vec<T, Capacity>`** — Fixed-capacity inline vector (no heap). `InlineVec::Append/Find/Erase/Insert`.
- **`array<T, Size>`** — Fixed-size array. `Array::Size/Front/Back`.
- **`inline_string<Capacity>`** — Fixed-cap string. `InlineString::Assign`.
- **No STL containers** — No `std::vector`, `std::string`, `std::map`. Use the above.
- **No exceptions** — Embedded-style. Use `ASSERT`/`TRAP`/`UNREACHABLE`.
- **No `new`/`delete`** — `MemPagePool::AcquireChunk()` + `CommitMemPages` for raw memory, `RegionAlloc::Alloc<T>` for typed allocation.
- **Platform files**: `*_linux.cc` / `*_windows.cc` convention. Platform-specific headers in `platform_linux.h` / `platform_windows.h`.

### Naming conventions (from .clang-tidy)
- Types (class/struct/enum): `CamelCase`
- Functions/methods: `CamelCase`
- Variables/params: `camelBack`
- Global constants: `kCamelCase`
- Global variables: `g_camelBack`
- Private members: `m_camelBack`
- Namespaces: `lower_case`
- All files: `snake_case.h` / `snake_case.cc`

### Compilation units
Each subdirectory has `CMakeListsGenerated.txt` listing source files. New `.cc`/`.h` files MUST be added there. Root `CMakeLists.txt` uses `GLOB_RECURSE` for `.cc`/`.h`/`.hlsl` — new directories need a glob entry.

### RHI (Rendering Hardware Interface)
`rhi.h` defines the GPU abstraction. Backends: `rhi_vulkan.cc` (Linux), `rhi_d3d12.cc` (Windows). Selected at compile time by Vulkan/D3D12 availability.

## Window Manager (`wm/`)

Single file: `wm/window_manager.cc` (~1950 lines). Entry point via `UserMain()`.

### Architecture
- Connects to X11 via xcb with `SUBSTRUCTURE_REDIRECT`
- 9 "stacks" (workspaces), one active at a time
- Infinite-strip horizontal tiling: windows placed at their `desiredWidth` pixels, scroll to keep active window visible
- Pure layout math in `ComputeInfiniteStripLayout()` — positions computed from window list + viewport width, no X11 calls
- Window data: `window_index_entry` (XID, flags, parent, data ptr) + `window_data_entry` (name, rect, geometry constraints)
- Binary search in sorted `window_index_entry[]` for O(log n) lookups
- Serialization: `WmSerialize()`/`WmDeserialize()` to `/tmp/nyla_wm_state` (binary format)
- IPC: `shm_channel` publishes `wm_ipc_state` to `/dev/shm/nyla_wm` for overlay

### Key bindings
| Combo | Action |
|---|---|
| Meta+Ltr | Window ops (move, zoom, follow) |
| Meta+←/→ | Cycle width tiers: 0.75× → 1× → 1.5× → full screen |
| Meta+Ctrl+←/→ | Switch stacks |
| Alt+Tab | Cycle windows |
| Meta+Shift+R | Restart WM (serialize + reconnect) |
| Alt+F4 | Close window (WM_DELETE_WINDOW) |

### Testing with Xvfb
The WM uses standard X11/xcb — no server-specific calls. Test strategy:
1. `Xvfb :99 -screen 0 1920x1080x24` → headless X server
2. `DISPLAY=:99 ./wm &` → launch WM as subprocess
3. Create test windows via xcb
4. Query geometry, input focus, window tree via xcb
5. Synthesize key events for keybinding tests
6. Kill WM + Xvfb on cleanup

### X11 helper API (platform_linux.h)
- `X11GetConn()` — xcb connection
- `X11GetRoot()` — root window
- `X11CreateWin(w,h,overrideRedirect,eventMask)` — create window
- `X11SendClientMessage32/SendWmTakeFocus/SendWmDeleteWindow/SendConfigureNotify`
- `X11KeyPhysicalToKeyCode/KeyCodeToKeyPhysical`
- `X11Grab/X11Ungrab/X11Flush`

### X11 protocol quirks (GDK interaction)

- **Event mask clobber in `ManageClient()`**: `xcb_change_window_attributes` with
  `CW_EVENT_MASK` **replaces** the entire mask. Always read the client's existing
  mask first (`xcb_get_window_attributes` → `your_event_mask`) and OR in the WM's
  required events. Losing `StructureNotifyMask` means GDK ignores synthetic
  `ConfigureNotify`. Losing `ButtonPress/PointerMotion` breaks GTK4 text selection.

- **Synthetic `ConfigureNotify` format**: `xcb_configure_notify_event_t` must have
  `.event = window` (both set). GDK checks `event == window` before processing.
  Zero-initialized `.event` silently drops the event.

- **`_NET_WM_MOVERESIZE`**: GTK4 GDK's resize emulation
  (`_gdk_x11_moveresize_handle_event`) **consumes** `ConfigureNotify` events during
  a drag — returns `TRUE` unconditionally, `resize_count` never decrements. Without
  `_NET_WM_MOVERESIZE` support, the WM must either send real `xcb_configure_window`
  (fighting the tiling layout) or implement `_NET_WM_MOVERESIZE` so GDK uses the
  EWMH path. `_NET_SUPPORTING_WM_CHECK` is required first — GDK won't check
  `_NET_SUPPORTED` without it.

- **`_NET_SUPPORTING_WM_CHECK`**: Create a 1×1 `InputOnly` window, map it, set
  `_NET_WM_NAME`, set self-referencing property, and set root property to point to
  it. Required for GDK's `gdk_x11_screen_supports_net_wm_hint()`.

- **`_NET_SUPPORTED`**: Only lists atoms we handle. Current set: `_NET_WM_MOVERESIZE`.
  `_NET_WM_STATE_HIDDEN`/`_NET_WM_STATE_FOCUSED` are irrelevant because WM uses
  off-screen hiding (not `xcb_unmap_window`) and sends focus via `xcb_set_input_focus`.

- **Resize tiers**: Meta+←/→ cycles: 0.75× → 1× (baseWidth) → 1.5× → full screen.
  Stops at edges, no wrap. Mouse border drag snaps to 160px increments.
  baseWidth is stable — separate from desiredWidth — so tier computation always
  has a valid anchor.

### RandR config_timestamp invalidation — DO NOT REORDER

`xcb_randr_set_crtc_config()` requires a `config_timestamp` that matches the
server's current configuration. Each `set_crtc_config` call CHANGES the config,
invalidating any previously obtained timestamp.

**Critical rule**: After a loop that calls `set_crtc_config()` (e.g. disabling
all outputs except the chosen one), you MUST obtain a fresh `config_timestamp`
via `xcb_randr_get_screen_resources()` before any subsequent `set_crtc_config()`
calls. Using a stale timestamp causes the call to silently fail (server returns
`BadConfig`), leaving the output disabled.

This applies to any edit that touches RandR code — reordering the disable/enable
sequence, adding intermediate round-trips, or inlining the function can all
introduce timestamp staleness. The safe pattern is:

```
resources2 = get_screen_resources()  // timestamp A
for each non-best output:            // each call changes config
    set_crtc_config(..., timestamp_A) // timestamp A progressively invalidated
resources3 = get_screen_resources()  // timestamp B (fresh)
set_crtc_config(best_output, ..., timestamp_B) // uses fresh timestamp
```

**Never** move the `set_crtc_config` for the best output before the disable
loop without also ensuring it uses a pre-disable timestamp, or it will fail
when the CRTC is still claimed by another output.

## Style rules
- `#pragma once` only (enforced by `scripts/check_pragma_once.sh`)
- Includes use `"nyla/commons/..."` paths (see `.clangd` QuotedHeaders)
- `// ─── Section ───` comment separators (unicode box-drawing chars)
- Trailing return types: `auto Func() -> ReturnType`
- `const` goes right: `int const* p` not `const int* p`
