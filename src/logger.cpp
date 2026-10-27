#include "logger.h"
#include <windows.h>

namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;
const DWORD kMaxLogBytes = 512 * 1024;   // 超过就重建，避免日志无限涨

// 统一按 UTF-8 落盘，用记事本/VSCode 打开都不乱码
std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}
} // namespace

void LogInit(const std::wstring& exeDir) {
    std::wstring dir = exeDir + L"\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);

    std::wstring path = dir + L"\\app.log";

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) &&
        fad.nFileSizeLow > kMaxLogBytes) {
        DeleteFileW(path.c_str());
    }

    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(const std::wstring& msg) {
    if (g_file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t head[32];
    swprintf(head, 32, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    std::wstring line = std::wstring(head) + msg + L"\r\n";
    std::string utf8 = ToUtf8(line);

    DWORD written = 0;
    WriteFile(g_file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
}

void LogClose() {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}
