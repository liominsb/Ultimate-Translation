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
    // 上下文窗口。长文本是在 TranslateText 里切块请求的，ctx 只要够装一块 + 输出即可，
    // 但调太小会反过来限制单块长度，默认给 8192 留足余量
    int ctx = 8192;
    int maxTokens = 256;      // 单块至少生成多少 token（实际按块长放大）
    int gpuLayers = 999;      // 999 = 全部层丢显卡
    // 官方推荐 0.7 / top_p 0.6 / top_k 20 / repeat_penalty 1.05
    double temperature = 0.7;
};

// 拉起子进程并等它就绪（轮询 /health）。已在运行则直接返回 true。
bool EngineStart(const EngineConfig& cfg);

// 终止子进程，释放显存
void EngineStop();

bool EngineRunning();

// 请求打断当前翻译。长文本是分块请求的，块与块之间检查这个标记；
// 已经发出去的那一块会自然结束（几秒内），不会留下半截状态。
void EngineCancel();

// 同步翻译，onDelta 在工作线程被逐段回调（UI 侧自行 PostMessage 回主线程）。
// 文本长度不设上限：超长内容在内部按句子切块、逐块请求，译文边译边回调。
// 返回值：完整译文；失败时返回已译出的部分并填 error。
std::wstring TranslateText(const std::wstring& text,
                           const std::wstring& srcLang,
                           const std::wstring& tgtLang,
                           const std::function<void(const std::wstring&)>& onDelta,
                           std::wstring& error);
