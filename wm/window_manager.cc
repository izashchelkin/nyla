#include <cstdint>
#include <xcb/xproto.h>

#include "nyla/commons/entrypoint.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/platform_linux.h" // IWYU pragma: keep
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

namespace
{

enum class Color : uint32_t
{
    KNone = 0x000000,
    KActive = 0x95A3B3,
    KReserved0 = 0xC0FF1F,
    KActiveFollow = 0x84DCC6,
    KReserved1 = 0x5E1FFF,
};

enum class layout_type : uint8_t
{
    Columns,
    Rows,
    Grid,
};

struct window_stack
{
    uint16_t flags;
    layout_type layout;
    xcb_window_t activeWindow;

    static inline constexpr decltype(flags) FlagZoom = 1 << 0;
};

struct window_data_entry;

struct window_index_entry
{
    xcb_window_t window;
    xcb_window_t parent;
    uint16_t flags;
    uint8_t stackIndexPlusOne;
    window_data_entry *dataEntry;

    static inline constexpr decltype(flags) Flag_WM_Hints_Input = 1 << 0;
    static inline constexpr decltype(flags) Flag_WM_TakeFocus = 1 << 1;
    static inline constexpr decltype(flags) Flag_WM_DeleteWindow = 1 << 2;
    static inline constexpr decltype(flags) Flag_Urgent = 1 << 3;
    static inline constexpr decltype(flags) Flag_WantsConfigureNotify = 1 << 4;
};

struct window_data_entry
{
    xcb_window_t window;
    uint32_t width;
    uint32_t height;
    uint32_t maxWidth;
    uint32_t maxHeight;
    inline_string<64> name;
};

struct window_manager
{
    uint8_t activeStackIndex;
    array<window_stack, 9> stacks;

    span<uint8_t> memory;
    uint32_t capacity;
    uint32_t windowCount;
    window_index_entry *index;
    window_data_entry *data;
};
window_manager *wm;

void IncreaseCapacity()
{
    MemMove(wm->index + (uint64_t)(wm->capacity * 2), wm->data, wm->capacity * sizeof(window_data_entry));
    wm->capacity *= 2;
    wm->data = (window_data_entry *)(wm->index + wm->capacity);
}

void DecreaseCapacity()
{
    wm->capacity /= 2;
    MemMove(wm->index + wm->capacity, wm->data, wm->capacity * sizeof(window_data_entry));
    wm->data = (window_data_entry *)(wm->index + wm->capacity);
}

} // namespace

void UserMain()
{
    wm = &RegionAlloc::Alloc<window_manager>(RegionAlloc::g_BootstrapAlloc);

    wm->memory = MemPagePool::AcquireChunk();
    wm->capacity = 16;
    wm->index = (window_index_entry *)wm->memory.data;
    wm->data = (window_data_entry *)(wm->index + wm->capacity);
    CommitMemPages(wm->memory.data, (uint8_t *)(wm->data + wm->capacity) - wm->memory.data);
}

} // namespace nyla