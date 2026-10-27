#include "config.h"
#include <cwctype>

static std::wstring IniPath(const std::wstring& exeDir) {
    return exeDir + L"\\config.ini";
}

static std::wstring ReadStr(const std::wstring& ini, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(L"general", key, def, buf, 511, ini.c_str());
    return buf;
}

static int ReadInt(const std::wstring& ini, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(L"general", key, def, ini.c_str());
}

static double ReadDouble(const std::wstring& ini, const wchar_t* key, double def) {
    // 系统 ini API 只有整数版，浮点值得自己转
    std::wstring s = ReadStr(ini, key, L"");
    if (s.empty()) return def;
    return wcstod(s.c_str(), nullptr);
}

static void WriteStr(const std::wstring& ini, const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(L"general", key, val.c_str(), ini.c_str());
}

void Config::Load(const std::wstring& exeDir) {
    std::wstring ini = IniPath(exeDir);
    hotkey = ReadStr(ini, L"hotkey", L"Alt+D");
    // target_lang 不落盘：Windows 的 ini API 按系统 ANSI 代码页写文件，
    // 写中文进去再用 UTF-8 编辑器打开就是乱码。方向用 ASCII 的 zh2en 表示。
    zh2en = ReadInt(ini, L"zh2en", 0) != 0;
    targetLang = zh2en ? L"英语" : L"中文";
    port = ReadInt(ini, L"port", 58088);
    maxTokens = ReadInt(ini, L"max_tokens", 256);
    temperature = ReadDouble(ini, L"temperature", 0.7);
    topP = ReadDouble(ini, L"top_p", 0.6);
    topK = ReadInt(ini, L"top_k", 20);
    repeatPenalty = ReadDouble(ini, L"repeat_penalty", 1.05);

    UINT m = MOD_ALT, k = 'D';
    if (ParseHotkey(hotkey, m, k)) {
        hotkeyMod = m;
        hotkeyVk = k;
    }
}

void Config::Save(const std::wstring& exeDir) const {
    std::wstring ini = IniPath(exeDir);
    WriteStr(ini, L"hotkey", hotkey);
    WriteStr(ini, L"zh2en", zh2en ? L"1" : L"0");
    WriteStr(ini, L"port", std::to_wstring(port));
    WriteStr(ini, L"max_tokens", std::to_wstring(maxTokens));

    // 浮点统一保留 2 位，避免 0.7000000001 这种噪音
    wchar_t tmp[32];
    swprintf(tmp, 32, L"%.2f", temperature);
    WriteStr(ini, L"temperature", tmp);
    swprintf(tmp, 32, L"%.2f", topP);
    WriteStr(ini, L"top_p", tmp);
    swprintf(tmp, 32, L"%.2f", repeatPenalty);
    WriteStr(ini, L"repeat_penalty", tmp);

    WriteStr(ini, L"top_k", std::to_wstring(topK));
}

static std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool ParseHotkey(const std::wstring& text, UINT& mod, UINT& vk) {
    UINT m = 0, key = 0;
    size_t start = 0;

    while (start <= text.size()) {
        size_t pos = text.find(L'+', start);
        std::wstring part = Trim(text.substr(start, (pos == std::wstring::npos) ? std::wstring::npos : pos - start));

        if (_wcsicmp(part.c_str(), L"Ctrl") == 0)       m |= MOD_CONTROL;
        else if (_wcsicmp(part.c_str(), L"Alt") == 0)   m |= MOD_ALT;
        else if (_wcsicmp(part.c_str(), L"Shift") == 0) m |= MOD_SHIFT;
        else if (_wcsicmp(part.c_str(), L"Win") == 0)   m |= MOD_WIN;
        else if (!part.empty()) {
            // 主键：单字符走大写，剩余只认 F1-F24 和几个常用键
            if (part.size() == 1) {
                key = (UINT)towupper(part[0]);
            } else if (part[0] == L'F' || part[0] == L'f') {
                int n = _wtoi(part.c_str() + 1);
                if (n >= 1 && n <= 24) key = VK_F1 + (n - 1);
            } else if (_wcsicmp(part.c_str(), L"Space") == 0) key = VK_SPACE;
            else if (_wcsicmp(part.c_str(), L"Enter") == 0)   key = VK_RETURN;
            else if (_wcsicmp(part.c_str(), L"Tab") == 0)     key = VK_TAB;
            else if (_wcsicmp(part.c_str(), L"`") == 0)       key = VK_OEM_3;
        }

        if (pos == std::wstring::npos) break;
        start = pos + 1;
    }

    if (key == 0) return false;
    mod = m;
    vk = key;
    return true;
}

std::wstring FormatHotkey(UINT mod, UINT vk) {
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl+";
    if (mod & MOD_ALT)     s += L"Alt+";
    if (mod & MOD_SHIFT)   s += L"Shift+";
    if (mod & MOD_WIN)     s += L"Win+";

    if (vk >= VK_F1 && vk <= VK_F24) {
        s += L"F" + std::to_wstring(vk - VK_F1 + 1);
    } else if (vk >= 'A' && vk <= 'Z') {
        s += (wchar_t)vk;
    } else if (vk >= '0' && vk <= '9') {
        s += (wchar_t)vk;
    } else {
        s += L"0x" + std::to_wstring(vk);
    }
    return s;
}
