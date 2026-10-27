#include "trayicon.h"

HICON CreateTrayIcon(int size) {
    if (size < 16) size = 16;

    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;

    int radius = size / 4;
    int margin = size / 16;

    // ---- 彩色层：深色圆角方块 + 白字 ----
    HDC colorDC = CreateCompatibleDC(screen);
    HBITMAP colorBmp = CreateCompatibleBitmap(screen, size, size);
    HGDIOBJ oldColorBmp = SelectObject(colorDC, colorBmp);

    // 先铺白底，圆角外的多余部分会被 mask 盖掉，不影响观感
    RECT rc{0, 0, size, size};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(colorDC, &rc, white);
    DeleteObject(white);

    HBRUSH bg = CreateSolidBrush(RGB(38, 38, 40));
    HPEN noPen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldBrush = SelectObject(colorDC, bg);
    HGDIOBJ oldPen = SelectObject(colorDC, noPen);
    RoundRect(colorDC, margin, margin, size - margin, size - margin, radius, radius);
    SelectObject(colorDC, oldBrush);
    SelectObject(colorDC, oldPen);
    DeleteObject(bg);
    DeleteObject(noPen);

    SetBkMode(colorDC, TRANSPARENT);
    SetTextColor(colorDC, RGB(255, 255, 255));
    SetTextAlign(colorDC, TA_CENTER | TA_TOP);

    int fontH = -(int)(size * 0.66f);
    HFONT font = CreateFontW(fontH, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(colorDC, font);
    TextOutW(colorDC, size / 2, (int)(size * 0.14f), L"译", 1);
    SelectObject(colorDC, oldFont);
    DeleteObject(font);

    SelectObject(colorDC, oldColorBmp);

    // ---- 掩码层：圆角内黑（不透明），圆角外白（透明）----
    HDC maskDC = CreateCompatibleDC(screen);
    HBITMAP maskBmp = CreateBitmap(size, size, 1, 1, nullptr);
    HGDIOBJ oldMaskBmp = SelectObject(maskDC, maskBmp);

    PatBlt(maskDC, 0, 0, size, size, WHITENESS);

    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ mOldBrush = SelectObject(maskDC, black);
    HGDIOBJ mOldPen = SelectObject(maskDC, GetStockObject(NULL_PEN));
    RoundRect(maskDC, margin, margin, size - margin, size - margin, radius, radius);
    SelectObject(maskDC, mOldBrush);
    SelectObject(maskDC, mOldPen);
    DeleteObject(black);

    SelectObject(maskDC, oldMaskBmp);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = maskBmp;
    info.hbmColor = colorBmp;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(colorBmp);
    DeleteObject(maskBmp);
    DeleteDC(colorDC);
    DeleteDC(maskDC);
    ReleaseDC(nullptr, screen);

    return icon;
}
