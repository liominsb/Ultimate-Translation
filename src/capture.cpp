#include "capture.h"
#include <windows.h>
#include <cstring>

// 发一组 Ctrl+C。用 SendInput 而不是 keybd_event：
// SendInput 是原子插入到输入流，不会被其它按键插队。
static void SendCtrlC() {
    INPUT in[4] = {};

    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;

    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = 'C';

    in[2].type = INPUT_KEYBOARD;
    in[2].ki.wVk = 'C';
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;

    in[3].type = INPUT_KEYBOARD;
    in[3].ki.wVk = VK_CONTROL;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, in, sizeof(INPUT));
}

// 只读 CF_UNICODETEXT。取不到就返回空串，调用方负责重试。
static std::wstring ReadClipboardText() {
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

static void WriteClipboardText(const std::wstring& s) {
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

// 等 Alt / Shift / Win 松开再复制。
// 用户按的是 Alt+D，热键触发那一瞬间 Alt 往往还压在手里；
// 这时候直接发 Ctrl+C，目标程序收到的是 Alt+Ctrl+C，
// 记事本这类老程序不挑，但 Chromium（Chrome / Electron / WorkBuddy）不认，复制根本不会发生。
static void WaitModifiersReleased() {
    for (int i = 0; i < 30; ++i) {
        bool down = (GetAsyncKeyState(VK_MENU) & 0x8000) ||
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
                    (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
                    (GetAsyncKeyState(VK_RWIN) & 0x8000);
        if (!down) return;
        Sleep(8);
    }
}

std::wstring CaptureSelectedText(int timeoutMs) {
    // 只备份纯文本。本产品只面向「选中一段文字再翻译」，
    // 剪贴板里原本是图片/文件的极端情况不做保护（已与需求方确认）。
    // 好处是省掉 OLE IDataObject 全量克隆，取词开销从几十 ms 降到几乎为零。
    std::wstring backup = ReadClipboardText();

    DWORD seq0 = GetClipboardSequenceNumber();

    WaitModifiersReleased();
    SendCtrlC();

    // 轮询剪贴板序列号。目标程序写剪贴板不是同步的，要给它时间。
    // 8ms 一次，兼顾延迟和 CPU（Sleep(0) 会空转烧一个核）。
    DWORD begin = GetTickCount();
    DWORD seq = seq0;
    while (GetTickCount() - begin < (DWORD)timeoutMs) {
        Sleep(8);
        seq = GetClipboardSequenceNumber();
        if (seq != seq0) break;
    }

    std::wstring text;
    if (seq != seq0) {
        // 序列号变了不代表数据已就绪（有的程序先占位再填），所以再轮询几次。
        for (int i = 0; i < 6 && text.empty(); ++i) {
            text = ReadClipboardText();
            if (text.empty()) Sleep(10);
        }
    }

    WriteClipboardText(backup);
    return text;
}
