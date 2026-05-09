#include "nyla/commons/clipboard.h"

#include "nyla/commons/mem.h"
#include "nyla/commons/platform_windows.h" // IWYU pragma: keep

namespace nyla
{

namespace Clipboard
{

auto API Read(span<uint8_t> out) -> uint64_t
{
    uint64_t len = 0;

    if (OpenClipboard(win32::WinGetHandle()))
    {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData)
        {
            const char *data = (const char *)GlobalLock(hData);
            if (data)
            {
                len = CStrLen(data, out.size);
                MemCpy(out.data, data, len);
                GlobalUnlock(hData);
            }
        }

        CloseClipboard();
    }

    return len;
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
