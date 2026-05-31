# WM Integration Tests — AI Context File

Integration tests (`test_wm.cc`) that run the real WM binary against Xvfb.

```
tests/wm/test_wm.cc    ← single-file test harness (~1900 lines)
tests/wm/AGENTS.md      ← this file
```

## Quick commands

```sh
# Build + run headless (Xvfb :98)
./scripts/test_wm.sh

# Or manually:
cmake --build build/linux-debug --target wm wm_test
./build/linux-debug/bin/wm_test

# Via ctest (must run from build dir):
cmake --build build/linux-debug --target wm wm_test
cd build/linux-debug && ctest -R wm_test --output-on-failure
```

## X11 server grab during WmInit

The WM brackets `WmInit()` with `X11Grab()`/`X11Ungrab()` + explicit `X11Flush()` after each. The grab prevents other X11 clients from racing with WM initialization (window tree query, key grabs). The ungrab must be flushed to the socket — without the flush, the ungrab sits in XCB's output buffer and the event loop deadlocks in `poll()` because no events arrive while the server is still grabbed.

The test handles this by:
1. Interning all X11 atoms **before** starting the WM (so no X11 ops are needed during the grab)
2. Sleeping 2s after WM fork to let `WmInit` + ungrab complete
3. Only then connecting to the X server

## FD_CLOEXEC for serialization restart

**Do NOT share an XCB connection fd with a forked WM process.** When the serialization restart test forks a new WM instance, the test's xcb socket must be marked `FD_CLOEXEC` to prevent the child from inheriting it:

```c
// Mark fd as close-on-exec so fork'd WM doesn't inherit the socket.
// Do NOT xcb_disconnect — that kills our test windows.
int fd = xcb_get_file_descriptor(conn);
int flags = fcntl(fd, F_GETFD);
if (flags != -1)
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

// Now safe to fork a new WM. Our conn and windows survive.
StartWm(display);
```

`xcb_disconnect` destroys all windows owned by that connection — a hard X11 protocol rule. FD_CLOEXEC is the correct approach.

## `xcb_poll_for_reply` — use blocking replies instead

`xcb_poll_for_reply` returns 1 if the reply is ready, 0 if not (non-blocking poll). In older xcb (<1.13) it only matched extension request sequence numbers correctly. For reliability across xcb versions, always use blocking `xcb_get_*_reply()` for core protocol requests. The time cost is negligible in tests.

## `NYLA_WM_NO_DAEMONS` — isolation from host WM

The WM reads this env var to suppress:

1. **Spawning/killing daemon processes** (wm_overlay, dunst, redshift, etc.)
2. **IPC channel creation** (`/dev/shm/nyla_wm`) — without this guard, the test WM would
   corrupt the host's status bar by writing test window titles to the shared memory
   that the real `wm_overlay` reads

Always set in `StartWm()`. Never remove it.

## Timing and races

### Real app startup ordering is nondeterministic

When a real app (xterm, xeyes) maps its window, the WM does **asynchronous** property fetches (WM_NAME, WM_CLASS, WM_TRANSIENT_FOR). Until all replies arrive, the WM may treat the window as a blank entry. A synthetic window created right after may appear to be managed first.

**Don't assert** ordering between real apps and synthetic windows. The invariant is that both eventually get tiled with sane sizes on screen.

### Focus tracking is racy

xterm may call `XSetInputFocus` after mapping, stealing focus from a previously-mapped window. The focus test accepts any of `{our_window_x, our_window_y, root}`.

## KeyboardTestBarrier — state between tests

Each keyboard test creates windows and injects XTEST keys. Leftover windows from a prior test would intercept key events. The barrier:
1. Destroys all windows with XID > 0x100000 (test-created windows)
2. Pumps the WM to process DestroyNotify events
3. Double-drains any stragglers

Always call `KeyboardTestBarrier()` at the start and end of every keybinding test.

## KillAllChildren — state between real-app tests

Tests that launch `xterm`/`xeyes`/`xclock` call `KillAllChildren()` first to clean up from prior real-app tests. Tests using synthetic windows only don't need it.

## XTEST key injection

- Uses `xcb/xtest.h` — must be enabled on Xvfb (`-ac` flag).
- xkbcommon provides key name → keycode lookup. Xvfb often lacks a core keyboard device via XI, so the test falls back to `evdev` rules + `us` layout.
- Modifier keycodes (Meta/Alt/Ctrl/Shift) are discovered via `xcb_get_modifier_mapping` at `Setup()` time.

## Serialization restart — the last test

`TestSerializationRestart()` must run **last** because it kills and restarts the WM. The new WM instance is a fresh process with no state from prior tests. The `Teardown()` function still runs, cleaning up the new WM.

Key flow:
1. Create windows w0, w1 on different stacks
2. Send Meta+Shift+R → WM serializes to `/tmp/nyla_wm_state` and exits
3. Wait for WM process to exit
4. **FD_CLOEXEC** the test's xcb fd (NOT disconnect!)
5. Fork new WM → reads `/tmp/nyla_wm_state` → re-manages existing windows
6. Verify windows have been assigned sizes by the revived WM
7. Unlink `/tmp/nyla_wm_state`

## Naming conventions (matches nyla/commons style)

- PascalCase functions: `CreateTestWindow`, `WaitForWindowClass`, `KeyboardTestBarrier`
- `kCamelCase` constants: `kScreenW`, `kMaxChildren`
- `camelBack` variables: `childCount`, `mod4Key`, `atomWmName`
- Anonymous namespace (not `static`) for file-scope functions and globals
- Trailing return types: `auto StartWm(...) -> bool`
- `char const*` (const goes right per project style)
- `nullptr` not `NULL`

## Process hierarchy

```
wm_test (parent)
  ├── Xvfb :98           ← headless X server
  ├── ./wm (DISPLAY=:98) ← WM under test
  ├── xterm              ← launched by LaunchApp()
  ├── xeyes
  └── xclock
```

All child processes are tracked in `children[]`. `KillAllChildren()` sends SIGTERM then `waitpid`s all of them. The WM and Xvfb are tracked separately in `wmPid` and `xvfbPid`.

## Display

Xvfb on `:98` (not `:99` — reserved for the user's real X server). Started with `-ac` (no access control).
