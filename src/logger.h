#pragma once
#include <string>

// 极简日志。存在就是为了出问题时能查：
// 以前引擎起不来只会弹一句「启动失败」，用户和我都只能猜是模型损坏、端口被占还是 DLL 缺失。
void LogInit(const std::wstring& exeDir);
void Log(const std::wstring& msg);
void LogClose();
