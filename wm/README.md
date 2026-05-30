# nyla-wm

A minimal tiling window manager for X11. Windows are laid out side-by-side in an infinite horizontal strip — new windows extend the strip, they never shrink existing ones.

## Highlights

- **Infinite strip** — windows at native pixel widths, scroll to keep the active one visible.
- **9 stacks (workspaces)** — each with its own window list and focus history.
- **Resize tiers** — `Meta+←/→` cycles 0.75× → 1× → 1.5× → full screen. Mouse border drag snaps to 160px steps.
- **Zoom mode** — `Meta+G` fills the viewport with the active window.
- **Ghostty-friendly** — `_NET_WM_MOVERESIZE`, EWMH compliance, event mask preservation.
- **Serialization** — layout, stacks, and per-window widths survive WM restart (`Meta+Shift+R`).
- **Integration tests** — real WM binary running against Xvfb.

## Build

```sh
cmake --preset linux-debug
cmake --build build/linux-debug --target wm
build/linux-debug/bin/wm
```

Test: `./scripts/test_wm.sh`  |  Restart: `Meta+Shift+R`