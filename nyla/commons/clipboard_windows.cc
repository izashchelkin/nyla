#include "nyla/commons/clipboard.h"

#include "nyla/commons/mem.h"
#include "nyla/commons/platform_windows.h" // IWYU pragma: keep
#include "nyla/commons/region_alloc.h"

namespace nyla
{

namespace Clipboard
{

auto API Read(region_alloc &alloc) -> byteview
{
    byteview ret = {};

    if (OpenClipboard(win32::WinGetHandle()))
    {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData)
        {
            const char *data = (const char *)GlobalLock(hData);
            if (data)
            {
                uint64_t len = CStrLen(data, 1_MiB);
                ret = RegionAlloc::CopyByteView(alloc, byteview{(const uint8_t *)data, len});
                GlobalUnlock(hData);
            }
        }

        CloseClipboard();
    }

    return ret;
}

void API Write(byteview text)
{
    if (OpenClipboard(win32::WinGetHandle()))
    {
        EmptyClipboard();

        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size + 1);
        if (hGlob)
        {
            char *data = (char *)GlobalLock(hGlob);
            MemCpy(data, text.data, text.size);
            data[text.size] = 0;
            GlobalUnlock(hGlob);

            if (!SetClipboardData(CF_TEXT, hGlob))
                GlobalFree(hGlob);
        }

        CloseClipboard();
    }
}

} // namespace Clipboard

} // namespace nyla
