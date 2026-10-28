#pragma once
#include <windows.h>
#include <string>

// 全局配置。来源是 exe 同目录的 config.ini，
// 用系统的 ini API 读写，不引 JSON 库，省一个依赖。
struct Config {
    std::wstring hotkey = L"Alt+D";   // 触发键，字符串形式便于显示和编辑
    UINT hotkeyMod = MOD_ALT;         // 解析后的修饰键
    UINT hotkeyVk = 'D';              // 解析后的主键
    std::wstring targetLang = L"中文"; // 目标语言，由 zh2en 决定
    bool zh2en = false;               // 反向模式：中文 -> 英文；关闭时是 英/日 -> 中文
    int port = 58088;                 // llama-server 端口
    int ctx = 8192;                   // 引擎上下文窗口，越小越省显存，但会限制单块长度
    int maxTokens = 256;              // 单块最低生成 token 数，实际按块长放大
    double temperature = 0.7;         // 官方推荐值
    double topP = 0.6;
    int topK = 20;
    double repeatPenalty = 1.05;

    void Load(const std::wstring& exeDir);
    void Save(const std::wstring& exeDir) const;
};

// "Alt+D" / "Ctrl+Shift+F2" -> 修饰键 + 虚拟键码
bool ParseHotkey(const std::wstring& text, UINT& mod, UINT& vk);

// 反方向，用于配置界面显示
std::wstring FormatHotkey(UINT mod, UINT vk);
