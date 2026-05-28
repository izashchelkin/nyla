#pragma once

#include <cstdint>

namespace nyla
{

// Shared state published by the window manager via shm_channel.
// Format: [shm_channel header: 8 bytes][wm_ipc_state]
//
// Add fields here as needed; overlay picks them up without protocol changes.
// Keep total size reasonable (<4K) — the overlay reads the whole struct each frame.
struct wm_ipc_state
{
    // Active window title, null-terminated. Empty string if no active window.
    char activeWindowTitle[256];

    // Which workspace (0-8) is active.
    uint8_t activeStackIndex;

    // Monotonically increasing counter incremented by the WM on every
    // shm write. The overlay compares this to detect state changes
    // without diffing the full payload.
    uint64_t updateGeneration;

    // Reserved for future use — zero-filled. Bump size when adding fields
    // above and reduce this padding to keep overall struct size stable.
    uint8_t _reserved[240];
};

static_assert(sizeof(wm_ipc_state) == 512, "wm_ipc_state must be 512 bytes");

} // namespace nyla
