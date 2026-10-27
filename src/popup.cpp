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
const int kTimerWatch = 2;
const UINT kAutoCloseMs = 12000;
const UINT kWatchMs = 50;

HINSTANCE g_inst = nullptr;
HWND g_hwnd = nullptr;
std::wstring g_source;
std::wstring g_result;
POINT g_anchor{0, 0};
HWND g_prevForeground = nullptr;
UINT g_dpi = 96;

// 字体和双缓冲位图都跨窗口复用。
// 原来是每次弹窗 CreateFontW ×2、每帧 CreateCompatibleBitmap，句柄数来回抖。
HFONT g_fontSrc = nullptr;
HFONT g_fontDst = nullptr;
UINT g_fontDpi = 0;

HDC g_memDC = nullptr;
HBITMAP g_memBmp = nullptr;
int g_bmpW = 0;
int g_bmpH = 0;

int g_srcHeight = 0;   // 原文高度只跟文本和宽度有关，一次翻译里不变，测一次就够

// 尺寸按内容走：一句话不该撑成 400 宽的空白框。
const int kPadX = 12;
const int kPadY = 8;
const int kGap = 4;
const int kMinTextW = 56;    // DIP，约三个汉字
const int kMaxTextW = 400;
const int kMaxSrcLines = 2;  // 原文只是给人对一下，两行以上没意义，反而把窗撑高

int g_textW = 0;      // 当前实际文字区宽度

// GetAsyncKeyState 的 0x0001 位是「自上次查询以来是否按下过」，
// 而程序第一次查询时它可能带着启动以来的历史状态，
// 会导致弹窗刚出现就被误判成「点了别处」。只认 0x8000，自己记录上一次的按下状态做边沿。
bool g_lastLBtnDown = false;

int Sc(int v) { return MulDiv(v, (int)g_dpi, 96); }

HFONT MakeFont(int height, LONG weight) {
    return CreateFontW(-Sc(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

void EnsureFonts(UINT dpi) {
    if (g_fontSrc && g_fontDpi == dpi) return;

    if (g_fontSrc) { DeleteObject(g_fontSrc); g_fontSrc = nullptr; }
    if (g_fontDst) { DeleteObject(g_fontDst); g_fontDst = nullptr; }

    g_dpi = dpi;
    g_fontDpi = dpi;
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

// 单行不换行时的自然宽度。
// 不能用 DT_CALCRECT 取 rc.right —— 那个值在需要换行时会退化成受限宽度，
// 拿去定窗口宽会算出一个又窄又高的框。
int NaturalWidth(HDC hdc, const std::wstring& s, HFONT font) {
    if (s.empty()) return 0;
    HGDIOBJ old = SelectObject(hdc, font);
    SIZE sz{0, 0};
    GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

// 宽度优先：短句收窄，长句封顶在 400。宽度定了再算高度。
void RecalcLayout(HDC hdc) {
    int maxTextW = Sc(kMaxTextW) - Sc(kPadX) * 2;

    int srcW = NaturalWidth(hdc, g_source, g_fontSrc);
    int dstW = NaturalWidth(hdc, g_result, g_fontDst);
    int textW = srcW > dstW ? srcW : dstW;

    if (textW < Sc(kMinTextW)) textW = Sc(kMinTextW);
    if (textW > maxTextW) textW = maxTextW;

    g_textW = textW;
    g_srcHeight = TextHeight(hdc, g_source, g_fontSrc, textW);

    // 原文封顶两行，超出就按两行高度裁掉，避免长原文把弹窗顶得很高
    int maxSrcH = Sc(17) * kMaxSrcLines;
    if (g_srcHeight > maxSrcH) g_srcHeight = maxSrcH;
}

int DesiredHeight(HDC hdc) {
    int h = Sc(kPadY) * 2;
    if (g_srcHeight > 0) h += g_srcHeight + Sc(kGap);
    h += TextHeight(hdc, g_result, g_fontDst, g_textW);
    // DT_CALCRECT 不含最后一份行距，中文换行时末行会被切，补一点余量
    return h + Sc(2);
}

void PlaceWindow(int w, int h) {
    int x = g_anchor.x + Sc(14);
    int y = g_anchor.y + Sc(18);

    // 超出当前显示器工作区就翻到鼠标另一侧
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

// 宽高都没变就不动窗口。流式输出时每个 token 都 SetWindowPos 会让窗口轻微抖动
void RelayoutIfNeeded() {
    if (!g_hwnd) return;

    HDC hdc = GetDC(g_hwnd);
    RecalcLayout(hdc);
    int h = DesiredHeight(hdc);
    ReleaseDC(g_hwnd, hdc);

    int w = g_textW + Sc(kPadX) * 2;

    RECT rc;
    GetClientRect(g_hwnd, &rc);
    if (h == rc.bottom && w == rc.right) return;

    PlaceWindow(w, h);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    // 双缓冲位图按尺寸复用，不必每帧 Create/Delete
    if (!g_memDC) g_memDC = CreateCompatibleDC(hdc);
    if (!g_memBmp || g_bmpW != w || g_bmpH != h) {
        if (g_memBmp) DeleteObject(g_memBmp);
        g_memBmp = CreateCompatibleBitmap(hdc, w, h);
        g_bmpW = w;
        g_bmpH = h;
        SelectObject(g_memDC, g_memBmp);
    }

    HDC mem = g_memDC;

    HBRUSH bg = CreateSolidBrush(RGB(250, 249, 246));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    SetBkMode(mem, TRANSPARENT);
    int x = Sc(kPadX), y = Sc(kPadY);
    int textW = g_textW;

    if (g_srcHeight > 0) {
        HGDIOBJ oldFont = SelectObject(mem, g_fontSrc);
        SetTextColor(mem, RGB(140, 140, 140));
        RECT r{x, y, x + textW, y + g_srcHeight};
        DrawTextW(mem, g_source.c_str(), -1, &r, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(mem, oldFont);
        y += g_srcHeight + Sc(kGap);
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
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            Paint(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;   // 背景全在双缓冲里画，不擦，避免闪白

        case WM_TIMER:
            if (wp == kTimerAutoClose) {
                PopupHide();
            } else if (wp == kTimerWatch) {
                // WS_EX_NOACTIVATE 的窗口永远不是前台窗口，收不到 WM_KILLFOCUS，
                // 「点别处关闭」只能主动轮询
                bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (down && !g_lastLBtnDown) {
                    POINT p;
                    GetCursorPos(&p);
                    RECT r;
                    GetWindowRect(hwnd, &r);
                    if (!PtInRect(&r, p)) {
                        PopupHide();
                        return 0;
                    }
                }
                g_lastLBtnDown = down;

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
            g_hwnd = nullptr;   // 字体和位图留着给下一次弹窗复用
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
        0, 0, Sc(200), Sc(60),   // 先给个小尺寸，马上按内容重算
        nullptr, nullptr, g_inst, nullptr);
    if (!g_hwnd) return;

    EnsureFonts(GetDpiForWindow(g_hwnd));

    // Win11 原生圆角；Win10 上调用会失败，退化为直角
    int pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    HDC hdc = GetDC(g_hwnd);
    RecalcLayout(hdc);
    int h = DesiredHeight(hdc);
    ReleaseDC(g_hwnd, hdc);
    PlaceWindow(g_textW + Sc(kPadX) * 2, h);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    SetTimer(g_hwnd, kTimerAutoClose, kAutoCloseMs, nullptr);
    g_prevForeground = GetForegroundWindow();
    // 记下当前按键状态：如果热键是鼠标点的，进来时左键可能还按着，
    // 不初始化会在第一次轮询时被当成「新按下」
    g_lastLBtnDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    SetTimer(g_hwnd, kTimerWatch, kWatchMs, nullptr);
}

void PopupSetContent(const std::wstring& source, const std::wstring& result) {
    if (!g_hwnd) return;

    g_source = source;
    if (g_source.size() > 160) {
        g_source = g_source.substr(0, 160) + L"…";
    }
    g_result = result;

    RelayoutIfNeeded();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void PopupSetResult(const std::wstring& result) {
    if (!g_hwnd) return;
    g_result = result;
    RelayoutIfNeeded();
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
