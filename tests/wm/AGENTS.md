# WM Integration Tests — AI Context File

Integration tests (`test_wm.cc`) that run the real WM binary against Xvfb.

```
tests/wm/test_wm.cc    ← single-file test harness (~1700 lines)
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

## Xvfb server grab deadlock — THE critical invariant

**Do NOT share an XCB connection or its fd with the WM process.** The root cause of the original hang was Xvfb failing to multiplex connections after a `xcb_grab_server()` / `xcb_ungrab_server()` cycle via `SUBSTRUCTURE_REDIRECT`.

The workaround in `WmInit()` is to **skip** `X11Grab()`/`X11Ungrab()` entirely. The WM debug output confirms this:

```
[wm] WmInit: X11Grab (skipped for test)
[wm] WmInit: X11Ungrab done (skipped for test)
```

**Important:** The invariant is still enforced at connection level. The test client's `conn` **must not exist** at the time the WM `fork()`s:

1. `Setup()` forks Xvfb :98
2. `Setup()` calls `StartWm()` — **no XCB connection exists yet**; the fork can't inherit one
3. WM finishes `WmInit()` (2 sec sleep)
4. `Setup()` now creates `xcb_connect(":98")` — fresh connection, never exposed to fork or grab

If you ever need to start a second WM instance mid-test (like the serialization restart test), mark the test's xcb fd as `FD_CLOEXEC` before forking to prevent socket inheritance — see "Serialization restart" below.

## `xcb_poll_for_reply` does NOT work for core protocol

`xcb_poll_for_reply` was added in xcb 1.13 for **extension** replies. It silently drops core protocol replies. Always use blocking `xcb_get_*_reply()` for core requests. The time it takes is negligible in tests.

## `xcb_disconnect` destroys all windows owned by the connection

Disconnecting an XCB connection tells the X server to destroy every window created on that connection. This is a hard X11 protocol rule.

In the serialization restart test, the test creates windows `w0` and `w1`, then the WM restarts. The test must keep those windows alive across the restart so the new WM can find and re-manage them. The fix is **FD_CLOEXEC**, not disconnect:

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
