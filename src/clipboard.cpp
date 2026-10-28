#include "clipboard.h"
#include <windows.h>
#include <cstring>

std::wstring ReadClipboardText() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = p;
            GlobalUnlock(h);
        }
    }

    CloseClipboard();
    return out;
}

void WriteClipboardText(const std::wstring& s) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    if (!s.empty()) {
        size_t bytes = (s.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
            // SetClipboardData 成功后内存所有权归系统，不能再 GlobalFree
        }
    }

    CloseClipboard();
}
