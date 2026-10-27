#pragma once
#include <windows.h>
#include <functional>
#include <string>

// 翻译引擎：管理 llama-server 子进程 + 走 HTTP 拿流式译文。
// 刻意不把推理塞进主进程：模型权重、KV cache、Vulkan 缓冲全部落在子进程，
// 主程序的私有内存才能压在个位数 MB。
struct EngineConfig {
    std::wstring runtimeDir;  // llama-server.exe 与 dll 所在目录
    std::wstring modelPath;   // gguf 绝对路径
    int port = 58088;
    int threads = 4;          // GPU 推理下 CPU 线程只影响 prompt 预处理
    int ctx = 512;            // 划词翻译单句足够，ctx 越大 KV cache 越吃内存
    int gpuLayers = 999;      // 999 = 全部层丢显卡
    // 官方推荐 0.7 / top_p 0.6 / top_k 20 / repeat_penalty 1.05
    double temperature = 0.7;
};

// 拉起子进程并等它就绪（轮询 /health）。已在运行则直接返回 true。
bool EngineStart(const EngineConfig& cfg);

// 终止子进程，释放显存
void EngineStop();

bool EngineRunning();

// 同步翻译，onDelta 在工作线程被逐段回调（UI 侧自行 PostMessage 回主线程）。
// 返回值：完整译文；失败时返回空串并填 error。
std::wstring TranslateText(const std::wstring& text,
                           const std::wstring& srcLang,
                           const std::wstring& tgtLang,
                           const std::function<void(const std::wstring&)>& onDelta,
                           std::wstring& error);
