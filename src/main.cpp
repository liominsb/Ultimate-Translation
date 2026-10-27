#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <string>

#include "config.h"
#include "capture.h"
#include "popup.h"
#include "langdetect.h"
#include "engine.h"
#include "logger.h"
#include "trayicon.h"

// MinGW 的头文件里没这个宏，值就是 SDK 里的 (DPI_AWARENESS_CONTEXT)-4
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#define WM_TRAYICON         (WM_APP + 1)
#define WM_TRANSLATE_DELTA  (WM_APP + 10)
#define WM_TRANSLATE_DONE   (WM_APP + 11)
#define WM_ENGINE_READY     (WM_APP + 12)
#define WM_TRANSLATE_SOURCE (WM_APP + 13)   // 取词完成，带回原文
#define WM_TRANSLATE_STATUS (WM_APP + 14)   // 只更新一行提示（取词失败/语种不支持等）

#define HOTKEY_ID_TRANSLATE 1
#define IDM_TRANSLATE 2001
#define IDM_RELOAD    2002
#define IDM_EXIT      2003

namespace {

Config g_cfg;
std::wstring g_exeDir;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid{};
std::atomic<bool> g_busy{false};
std::atomic<bool> g_engineReady{false};
std::atomic<bool> g_engineStarting{false};
HICON g_trayIcon = nullptr;
std::wstring g_accum;   // 当前这轮翻译已收到的译文，每次新翻译必须清空

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    return s.substr(b, e - b);
}

// 挑一个可用的 gguf。
// 注意：官方的 1.25bit / 2bit 版本依赖 STQ kernel（未并入 llama.cpp 主线），
// 现有运行时加载会直接报 tensor offset 错误，所以必须跳过。
std::wstring FindModelFile() {
    std::wstring pattern = g_exeDir + L"\\models\\*.gguf";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";

    std::wstring fallback;
    do {
        std::wstring name = fd.cFileName;
        std::wstring lower = name;
        for (auto& c : lower) c = (wchar_t)towlower(c);

        if (lower.find(L"1.25bit") != std::wstring::npos ||
            lower.find(L"2bit") != std::wstring::npos) {
            continue;   // STQ 专用格式，跳过
        }

        std::wstring full = g_exeDir + L"\\models\\" + name;
        if (lower.find(L"q8_0") != std::wstring::npos ||
            lower.find(L"q6_k") != std::wstring::npos ||
            lower.find(L"q5_k") != std::wstring::npos ||
            lower.find(L"q4_k") != std::wstring::npos) {
            FindClose(h);
            return full;   // 命中标准量化，优先
        }
        if (fallback.empty()) fallback = full;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return fallback;
}

void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_trayIcon = CreateTrayIcon(32);
    g_nid.hIcon = g_trayIcon ? g_trayIcon : LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"终极翻译", ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
}

// 用气泡而不是 MessageBox：失败时不能把消息循环卡死，
// 用户点掉对话框之前连热键都按不动
void NotifyBalloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_ERROR;
    lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// 引擎预热放在后台：模型加载实测 3 秒，不能卡住托盘和热键响应
DWORD WINAPI EngineWarmupThread(LPVOID) {
    EngineConfig cfg;
    cfg.runtimeDir = g_exeDir + L"\\runtime";
    cfg.modelPath = FindModelFile();
    cfg.port = g_cfg.port;
    cfg.threads = 4;
    cfg.ctx = 512;
    cfg.gpuLayers = 999;
    cfg.temperature = g_cfg.temperature;

    bool ok = !cfg.modelPath.empty() && EngineStart(cfg);
    g_engineReady = ok;
    g_engineStarting = false;
    if (!ok) Log(L"引擎预热失败，可再按热键重试");
    PostMessageW(g_hwnd, WM_ENGINE_READY, ok ? 1 : 0, 0);
    return 0;
}

// 异步拉起引擎，重复调用会被挡掉，避免并发起一堆 llama-server
void StartEngineAsync() {
    if (g_engineStarting.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, EngineWarmupThread, nullptr, 0, nullptr));
}

// 工作线程：取词 -> 校验 -> 翻译。全程不占主线程，
// 主线程只负责收消息、画窗，所以按热键时界面不会卡。
DWORD WINAPI WorkThread(LPVOID param) {
    delete (POINT*)param;   // 位置在主线程按热键时就定了，这里只是接手所有权

    DWORD tStart = GetTickCount();
    std::wstring text = Trim(CaptureSelectedText());
    DWORD tCap = GetTickCount();

    if (text.empty()) {
        PostMessageW(g_hwnd, WM_TRANSLATE_STATUS, 0,
                     (LPARAM)new std::wstring(L"没有取到选中的文字"));
        g_busy = false;
        return 0;
    }

    Lang lang = DetectLang(text);
    if (lang == Lang::Chinese) {
        PostMessageW(g_hwnd, WM_TRANSLATE_SOURCE, 0, (LPARAM)new std::wstring(text));
        PostMessageW(g_hwnd, WM_TRANSLATE_STATUS, 0,
                     (LPARAM)new std::wstring(L"选中的已经是中文"));
        g_busy = false;
        return 0;
    }
    if (lang == Lang::Unknown) {
        PostMessageW(g_hwnd, WM_TRANSLATE_SOURCE, 0, (LPARAM)new std::wstring(text));
        PostMessageW(g_hwnd, WM_TRANSLATE_STATUS, 0,
                     (LPARAM)new std::wstring(L"目前只支持英文和日文"));
        g_busy = false;
        return 0;
    }
    // 误全选整页时会取到几千字，硬塞给 ctx=512 的模型只会被截断成半句，
    // 宁可明确拒绝，也别给出残缺译文
    if (text.size() > 500) {
        PostMessageW(g_hwnd, WM_TRANSLATE_SOURCE, 0, (LPARAM)new std::wstring(text));
        PostMessageW(g_hwnd, WM_TRANSLATE_STATUS, 0,
                     (LPARAM)new std::wstring(L"选中的内容太长（超过 500 字），请分段选择"));
        g_busy = false;
        return 0;
    }

    // 先把原文填进弹窗，用户能立刻看到确实抓到了什么
    PostMessageW(g_hwnd, WM_TRANSLATE_SOURCE, 0, (LPARAM)new std::wstring(text));

    // 不能只看 g_engineReady：llama-server 可能已经崩了或者被手动杀了
    if (!g_engineReady || !EngineRunning()) {
        if (g_engineReady) {
            Log(L"引擎进程已不存在，标记为未就绪");
            g_engineReady = false;
        }
        bool starting = g_engineStarting.load();
        StartEngineAsync();
        PostMessageW(g_hwnd, WM_TRANSLATE_STATUS, 0,
                     (LPARAM)new std::wstring(starting
                         ? L"引擎正在启动，请稍候再试"
                         : L"正在启动翻译引擎，几秒后再按一次 " + g_cfg.hotkey));
        g_busy = false;
        return 0;
    }

    std::wstring err;
    DWORD begin = GetTickCount();

    // 流式节流：首字立刻发，之后每 16ms 合并一次。
    // 不节流的话一句话要触发三十来次重排和重绘。
    std::wstring pending;
    DWORD lastPost = GetTickCount();
    bool sentAny = false;

    auto onDelta = [&](const std::wstring& piece) {
        pending += piece;
        DWORD now = GetTickCount();
        if (!sentAny || now - lastPost >= 16 || pending.size() >= 12) {
            PostMessageW(g_hwnd, WM_TRANSLATE_DELTA, 0, (LPARAM)new std::wstring(pending));
            pending.clear();
            lastPost = now;
            sentAny = true;
        }
    };

    TranslateText(text, LangName(lang), g_cfg.targetLang, onDelta, err);

    if (!pending.empty()) {
        PostMessageW(g_hwnd, WM_TRANSLATE_DELTA, 0, (LPARAM)new std::wstring(pending));
    }

    DWORD capMs = tCap - tStart;
    if (capMs > 200) Log(L"取词偏慢：" + std::to_wstring(capMs) + L"ms");
    // 正常路径不写日志，只有出错才留痕
    if (!err.empty()) {
        Log(L"翻译失败（" + std::to_wstring(text.size()) + L" 字）：" + err);
    } else {
        DWORD used = GetTickCount() - begin;
        if (used > 3000) Log(L"翻译偏慢：" + std::to_wstring(used) + L"ms");
    }

    PostMessageW(g_hwnd, WM_TRANSLATE_DONE, 0, (LPARAM)new std::wstring(err));
    g_busy = false;
    return 0;
}

void DoTranslate() {
    if (g_busy.exchange(true)) return;  // 上一句还在翻，忽略连按

    POINT pt;
    GetCursorPos(&pt);

    // 立刻弹窗给反馈。取词要等目标程序写剪贴板，快的 20ms、Chromium 慢的 200ms，
    // 这段空窗期用户本来对着桌面发呆。
    g_accum.clear();
    PopupShow(pt, L"", L"正在读取选中文字…");

    CloseHandle(CreateThread(nullptr, 0, WorkThread, new POINT(pt), 0, nullptr));
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TRANSLATE,
                (L"翻译选中文字\t" + g_cfg.hotkey).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_RELOAD, L"重载翻译引擎");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    // 必须先置前台，否则点菜单外面收不起来（Win32 老坑）
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_HOTKEY:
            if (wp == HOTKEY_ID_TRANSLATE) DoTranslate();
            return 0;

        case WM_TRAYICON:
            if (LOWORD(lp) == WM_RBUTTONUP) ShowTrayMenu(hwnd);
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) DoTranslate();
            return 0;

        case WM_TRANSLATE_SOURCE: {
            std::wstring* src = (std::wstring*)lp;
            if (PopupIsVisible()) PopupSetContent(*src, L"翻译中…");
            delete src;
            return 0;
        }

        case WM_TRANSLATE_STATUS: {
            std::wstring* hint = (std::wstring*)lp;
            if (PopupIsVisible()) PopupSetResult(*hint);
            delete hint;
            return 0;
        }

        case WM_TRANSLATE_DELTA: {
            std::wstring* piece = (std::wstring*)lp;
            if (PopupIsVisible()) {
                g_accum += *piece;
                PopupSetResult(g_accum);
            }
            delete piece;
            return 0;
        }

        case WM_TRANSLATE_DONE: {
            std::wstring* err = (std::wstring*)lp;
            if (!err->empty() && PopupIsVisible()) {
                PopupSetResult(L"翻译失败：" + *err);
            }
            delete err;
            return 0;
        }

        case WM_ENGINE_READY:
            if (!wp) {
                NotifyBalloon(L"翻译引擎启动失败",
                              L"详情见 logs\\app.log；再按一次热键会自动重试");
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDM_TRANSLATE:
                    DoTranslate();
                    break;
                case IDM_RELOAD:
                    EngineStop();
                    g_engineReady = false;
                    StartEngineAsync();
                    break;
                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;

        case WM_DESTROY:

            EngineStop();
            RemoveTrayIcon();
            PopupHide();
            LogClose();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // 单实例：重复启动会注册两个相同的全局热键，必须挡掉
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"TranslateBall.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_exeDir = GetExeDir();
    LogInit(g_exeDir);
    g_cfg.Load(g_exeDir);

    // 首次运行落一份 config.ini，让用户知道哪些项可调，
    // 而不是只能靠改源码
    if (GetFileAttributesW((g_exeDir + L"\\config.ini").c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_cfg.Save(g_exeDir);
    }

    PopupInit(hInst);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"TranslateBallMain";
    RegisterClassExW(&wc);

    // 窗口只用来收消息、永不显示；
    // 但不能用 HWND_MESSAGE，否则托盘菜单的 SetForegroundWindow 会失效
    g_hwnd = CreateWindowExW(0, L"TranslateBallMain", L"终极翻译",
                             WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    if (!RegisterHotKey(g_hwnd, HOTKEY_ID_TRANSLATE, g_cfg.hotkeyMod, g_cfg.hotkeyVk)) {
        MessageBoxW(nullptr,
                    (L"全局快捷键 " + g_cfg.hotkey + L" 注册失败，可能已被其它程序占用。\n"
                     L"请修改 config.ini 里的 hotkey 后重启。").c_str(),
                    L"终极翻译", MB_ICONWARNING);
    }

    AddTrayIcon(g_hwnd);


    // 引擎异步预热，不阻塞消息循环
    StartEngineAsync();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    UnregisterHotKey(g_hwnd, HOTKEY_ID_TRANSLATE);
    if (mutex) CloseHandle(mutex);
    return 0;
}
