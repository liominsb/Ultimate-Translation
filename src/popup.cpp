#include "popup.h"
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

const wchar_t* kClassName = L"TranslateBallPopup";
const int kTimerAutoClose = 1;
const int kTimerWatch = 2;      // 轮询「点别处 / 切窗口」，50ms 一次开销可忽略
const UINT kAutoCloseMs = 12000;
const UINT kWatchMs = 50;

HINSTANCE g_inst = nullptr;
HWND g_hwnd = nullptr;
std::wstring g_source;
std::wstring g_result;
POINT g_anchor{0, 0};
HWND g_prevForeground = nullptr;   // 弹窗出现时的前台窗口，变了就说明用户切走了
UINT g_dpi = 96;
HFONT g_fontSrc = nullptr;
HFONT g_fontDst = nullptr;

int Sc(int v) { return MulDiv(v, (int)g_dpi, 96); }

void FreeFonts() {
    if (g_fontSrc) { DeleteObject(g_fontSrc); g_fontSrc = nullptr; }
    if (g_fontDst) { DeleteObject(g_fontDst); g_fontDst = nullptr; }
}

HFONT MakeFont(int height, LONG weight) {
    return CreateFontW(-Sc(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

void EnsureFonts() {
    // 字体句柄跟 DPI 绑定，换屏/换缩放必须重建，不能全局缓存
    FreeFonts();
    g_fontSrc = MakeFont(12, FW_NORMAL);
    g_fontDst = MakeFont(15, FW_NORMAL);
}

int TextHeight(HDC hdc, const std::wstring& s, HFONT font, int width) {
    if (s.empty()) return 0;
    HGDIOBJ old = SelectObject(hdc, font);
    RECT rc{0, 0, width, 0};
    DrawTextW(hdc, s.c_str(), (int)s.size(), &rc,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    return rc.bottom;
}

int DesiredHeight(HDC hdc) {
    int textW = Sc(400) - Sc(28);
    int h = Sc(28);
    if (!g_source.empty()) {
        h += TextHeight(hdc, g_source, g_fontSrc, textW) + Sc(8);
    }
    h += TextHeight(hdc, g_result, g_fontDst, textW);
    // DT_CALCRECT 算出来的高度不含行间距的最后一份，
    // 中文换行时最后一行总被切掉半截，这里补一行余量
    return h + Sc(12);
}

void Relayout() {
    if (!g_hwnd) return;

    HDC hdc = GetDC(g_hwnd);
    int h = DesiredHeight(hdc);
    ReleaseDC(g_hwnd, hdc);

    int w = Sc(400);
    int x = g_anchor.x + Sc(14);
    int y = g_anchor.y + Sc(18);

    // 超出当前显示器工作区就翻到鼠标另一侧，避免弹窗跑出屏幕
    MONITORINFO mi{sizeof(mi)};
    HMONITOR mon = MonitorFromPoint(g_anchor, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        const RECT& wa = mi.rcWork;
        if (x + w > wa.right)  x = g_anchor.x - w - Sc(14);
        if (y + h > wa.bottom) y = g_anchor.y - h - Sc(18);
        if (x < wa.left) x = wa.left;
        if (y < wa.top)  y = wa.top;
    }

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    // 双缓冲：直接往 hdc 画会闪
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(250, 249, 246));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    SetBkMode(mem, TRANSPARENT);
    int x = Sc(14), y = Sc(14);
    int textW = w - Sc(28);

    if (!g_source.empty()) {
        HGDIOBJ oldFont = SelectObject(mem, g_fontSrc);
        SetTextColor(mem, RGB(140, 140, 140));
        RECT r{x, y, x + textW, y};
        DrawTextW(mem, g_source.c_str(), -1, &r,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        DrawTextW(mem, g_source.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
        y = r.bottom + Sc(8);
        SelectObject(mem, oldFont);
    }

    HGDIOBJ oldFont = SelectObject(mem, g_fontDst);
    SetTextColor(mem, RGB(32, 32, 32));
    RECT r{x, y, x + textW, h};
    DrawTextW(mem, g_result.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(mem, oldFont);

    HBRUSH border = CreateSolidBrush(RGB(228, 226, 222));
    FrameRect(mem, &rc, border);
    DeleteObject(border);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            Paint(hwnd);
            return 0;

        case WM_ERASEBKGND:
            // 背景全在 WM_PAINT 的双缓冲里画，这里不擦，避免闪白
            return 1;

        case WM_TIMER:
            if (wp == kTimerAutoClose) {
                PopupHide();
            } else if (wp == kTimerWatch) {
                // 弹窗是 WS_EX_NOACTIVATE，永远不会成为前台窗口，
                // 所以收不到 WM_KILLFOCUS，「点别处关闭」只能主动轮询。
                SHORT st = GetAsyncKeyState(VK_LBUTTON);
                bool pressed = (st & 0x8000) || (st & 0x0001);

                if (pressed) {
                    POINT p;
                    GetCursorPos(&p);
                    RECT r;
                    GetWindowRect(hwnd, &r);
                    if (!PtInRect(&r, p)) {
                        PopupHide();   // 点在了弹窗外面
                        return 0;
                    }
                }

                // 切到别的窗口也关掉，不然它一直浮着挡人
                HWND fg = GetForegroundWindow();
                if (fg && g_prevForeground && fg != g_prevForeground) {
                    PopupHide();
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            PopupHide();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kTimerAutoClose);
            KillTimer(hwnd, kTimerWatch);
            FreeFonts();
            g_hwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void PopupInit(HINSTANCE hInst) {
    g_inst = hInst;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = PopupProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

void PopupShow(POINT screenPt, const std::wstring& source, const std::wstring& result) {
    PopupHide();

    g_anchor = screenPt;
    g_source = source;
    g_result = result;

    // 原文太长会撑爆小窗，截断展示即可，完整翻译不受影响
    if (g_source.size() > 160) {
        g_source = g_source.substr(0, 160) + L"…";
    }

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"", WS_POPUP,
        0, 0, Sc(400), Sc(80),
        nullptr, nullptr, g_inst, nullptr);
    if (!g_hwnd) return;

    g_dpi = GetDpiForWindow(g_hwnd);
    EnsureFonts();

    // Win11 原生圆角；Win10 上这个调用会失败，退化为直角，可接受
    int pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    Relayout();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    SetTimer(g_hwnd, kTimerAutoClose, kAutoCloseMs, nullptr);

    // 记录此刻的前台窗口，之后它一变就说明用户切走了
    g_prevForeground = GetForegroundWindow();
    SetTimer(g_hwnd, kTimerWatch, kWatchMs, nullptr);
}

void PopupSetResult(const std::wstring& result) {
    if (!g_hwnd) return;
    g_result = result;
    Relayout();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void PopupHide() {
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

bool PopupIsVisible() {
    return g_hwnd != nullptr;
}
