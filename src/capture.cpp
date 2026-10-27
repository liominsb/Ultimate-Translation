#include "capture.h"
#include <windows.h>
#include <cstring>

namespace {

// 发一组 Ctrl+C。用 SendInput 而不是 keybd_event：
// SendInput 把整组按键原子地插进输入流，不会被其它按键插队。
void SendCtrlC() {
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

bool AnyModifierDown() {
    return (GetAsyncKeyState(VK_MENU) & 0x8000) ||
           (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
           (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
           (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

// 等 Alt / Shift / Win 松开再复制。
// 用户按的是 Alt+D，热键触发那一瞬间 Alt 往往还压在手里；
// 这时候直接发 Ctrl+C，目标程序收到的是 Alt+Ctrl+C，
// 记事本这类老程序不挑，但 Chromium（Chrome / Electron）不认，复制根本不会发生。
//
// 2ms 粒度：用户松手通常 30-50ms，8ms 一档会白等平均 4ms。
void WaitModifiersReleased() {
    for (int i = 0; i < 60; ++i) {   // 上限 120ms
        if (!AnyModifierDown()) return;
        Sleep(2);
    }
}

} // namespace

std::wstring CaptureSelectedText(int timeoutMs) {
    // 只备份纯文本。本产品只面向「选中一段文字再翻译」，
    // 剪贴板里原本是图片/文件的极端情况不做保护（已与需求方确认）。
    std::wstring backup = ReadClipboardText();

    WaitModifiersReleased();

    DWORD seq0 = GetClipboardSequenceNumber();
    SendCtrlC();

    // 轮询剪贴板序列号。
    // 试过 AddClipboardFormatListener + 事件，能省 4-8ms，但会和「上一轮恢复剪贴板」
    // 触发的信号打架，导致取到旧内容，稳定性不值这点延迟，退回轮询。
    // 4ms 一档：目标程序写剪贴板的耗时本身就有 20-60ms，再细意义不大。
    DWORD begin = GetTickCount();
    DWORD seq = seq0;
    while (GetTickCount() - begin < (DWORD)timeoutMs) {
        Sleep(4);
        seq = GetClipboardSequenceNumber();
        if (seq != seq0) break;
    }

    std::wstring text;
    if (seq != seq0) {
        // 序列号变了不代表数据已就绪（有的程序先占位再填），补几次
        for (int i = 0; i < 6 && text.empty(); ++i) {
            text = ReadClipboardText();
            if (text.empty()) Sleep(5);
        }
    }

    WriteClipboardText(backup);
    return text;
}
