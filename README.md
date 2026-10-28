# 终极翻译

Windows 划词翻译工具。选中任意语言的文字，按 `Alt+D`，鼠标旁弹出中文译文。

本地推理，不联网。默认模型是腾讯混元 Hy-MT2-1.8B（Q6_K 量化），通过 Vulkan 跑在显卡上。

模型只是推荐，不是绑定——任何 llama.cpp 能跑的 GGUF 都可以换，见「换模型」。

主程序私有内存约 3.5MB——模型跑在独立的 `llama-server` 子进程里，和界面完全隔离。

## 下载

到 [Releases](https://github.com/liominsb/Ultimate-Translation/releases) 下载 `Ultimate-Translation-v1.0.0-win64.zip`，解压即用。包里含主程序和推理运行时，约 30MB。

**模型需要单独下载**。1.4GB 不随发行版分发，从 [ModelScope](https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF) 取 `Hy-MT2-1.8B-Q6_K.gguf` 放进 `models/`：

```
https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/resolve/master/Hy-MT2-1.8B-Q6_K.gguf
```

解压后的结构：

```
Ultimate-Translation/
├─ TranslateBall.exe
├─ README.md
├─ config.ini                       首次运行自动生成
├─ runtime/                         推理运行时，发行版已包含
└─ models/
   └─ Hy-MT2-1.8B-Q6_K.gguf         ← 需要自己下
```

`runtime/` 和 `models/` 必须和 exe 放在同一层，不能挪走。

## 用法

1. 双击 `TranslateBall.exe`，托盘出现图标，等 3 秒左右模型加载完成
2. 在任何程序里用鼠标选中想翻译的文字
3. 按 `Alt+D`
4. 弹窗出现在鼠标右下角，译文流式输出；**不会自动消失**，看完自己关
5. 想留用译文：**单击弹窗任意位置**即可整段复制到剪贴板（右键也行），底部会提示「已复制」
6. 关闭方式：点窗口外面的任意地方，或者切到别的窗口

**语种不限于英日**。程序本身只区分「中文 / 非中文」，具体是什么语言交给模型识别。默认的 Hy-MT2 支持 33 种语言，英、日、韩、法、德、俄实测都能正确翻译。日语因为假名特征明显，会被准确标出源语言。

托盘图标右键有：翻译选中文字 / 中文翻译英文（勾选=中译英，不勾=英日译中）/ 重载翻译引擎 / 退出。

方向切换会记到 `config.ini`，下次启动保持。

## 配置

改完 `config.ini` 需要重启程序。

| 项 | 默认 | 说明 |
|---|---|---|
| `hotkey` | `Alt+D` | 支持 `Ctrl+Shift+X`、`F1`~`F24` 这类写法 |
| `zh2en` | `0` | `0` = 外语 → 中；`1` = 中 → 英。托盘菜单那个勾选项改的就是它 |
| `port` | `58088` | 引擎监听端口，被占用就换一个 |
| `ctx` | `8192` | 引擎上下文窗口，要装下「800 字一块 + 最多 1600 token 译文」。分块大小写在代码里，调小 `ctx` 不会让块变小、只会压掉余量，4096 是下限 |
| `temperature` | `0.70` | 官方推荐值，想要更稳定的输出可以降到 0.3 |
| `max_tokens` | `256` | 单块**最低**生成长度，实际按块内字数自动放大，一般不用动 |

目标语言不直接写中文，用 `zh2en` 开关表示——Windows 的 ini API 按系统 ANSI 码页写文件，中文值用 UTF-8 编辑器打开就是乱码。

## 换模型

默认的 Hy-MT2 只是推荐，不是绑定。任何 llama.cpp 能加载的 GGUF 都能用，丢进 `models/` 即可，程序会自动挑一个标准量化版本（Q4_K_M / Q5_K / Q6_K / Q8_0 优先）。放多个时挑中哪个由文件名匹配决定，建议只留一个。

换别的模型要注意三点：必须是 GGUF 格式；GGUF 里要带 `tokenizer.chat_template`（llama.cpp 靠它拼对话）；`ctx` 默认 8192，是按「单块 800 字 + 最多 1600 token 输出」留的余量，换分词更碎的模型可以往上调。

**注意**：官方的 `1.25bit` 和 `2bit` 版本不能用。它们依赖腾讯自研的 STQ kernel（llama.cpp PR #22836），还没并进主线，现有运行时会直接报错：

```
gguf_init_from_reader: tensor 'blk.0.attn_k_norm.weight' has offset ... expected ...
```

显存占用：Q6_K 约 1.8GB，Q8_0 约 2.4GB，8GB 显卡都够。

速度参考（i9-14900HX + RTX 4060 Laptop，Q6_K，Vulkan 全量 offload）：生成约 113 tok/s，单句端到端 0.07–0.2 秒。

## 从源码构建

需要 MinGW-w64（g++ 13 以上）和 CMake + Ninja。

**构建目录不能含中文**：MinGW Makefiles 处理不了，必须用 Ninja。

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物 `build/TranslateBall.exe`，复制到 `runtime/` 和 `models/` 的同级目录即可。

`runtime/` 不在仓库里，从 llama.cpp 官方 Windows Vulkan 构建取：

<https://github.com/ggml-org/llama.cpp/releases/tag/b11118>

选 `llama-b11118-bin-win-vulkan-x64.zip`（约 30MB），解压后把里面的 `.exe` 和 `.dll` 全部放进 `runtime/`。

必须齐全：`llama-server.exe`、`llama-server-impl.dll`、`llama-common.dll`、`llama.dll`、`ggml.dll`、`ggml-base.dll`、`ggml-cpu-*.dll`、`ggml-vulkan.dll`、`mtmd.dll`、`libomp.dll`。

目标机器如果没装 VC++ 2015-2022 运行库，还要把 `api-ms-win-crt-*.dll`、`ucrtbase.dll`、`VCRUNTIME140*.dll` 一并放进去，否则启动只报一句 `error while loading shared libraries`，不会告诉你缺哪个。

## 已知限制

- 取词靠模拟 Ctrl+C，会短暂占用剪贴板（约 100ms），之后原样恢复
- 剪贴板里原本是图片/文件时，会被这次取词覆盖掉
- 文本长度不设上限，但整页长文是一块一块翻的，速度按块数累加；按一次热键可打断当前这轮
- 译文超过屏幕高度时弹窗高度封顶，屏幕上看不全，单击即可复制全文
- 默认方向是「外语 → 中文」，选中中文会提示切换；切到中译英后只处理中文
- 界面只区分「中文 / 非中文」，具体语种交给模型识别，能翻哪些取决于换的是什么模型
- 朗读功能未实现
- 只支持 Windows 10 及以上

## 排错

出问题先看 `logs/app.log`（UTF-8，超过 512KB 自动重建）。正常运行时不写任何内容，只在这几种情况下留痕：

- 引擎启动失败（模型缺失、DLL 缺失、端口被占、进程提前退出）
- 引擎进程崩溃后被自动拉起
- 翻译请求失败
- 翻译耗时超过 3 秒

常见情况：

| 现象 | 原因 |
|---|---|
| 双击没反应，托盘也没图标 | 已经有一个实例在跑（单实例互斥），看托盘 |
| 托盘气泡「引擎启动失败」 | 看日志里的具体行，通常是 models 目录没有 gguf、runtime 缺 DLL，或者端口被占 |
| 提示「正在启动翻译引擎」 | 引擎崩了在自动重启，等两秒再按一次热键即可 |
| 提示「没有取到选中的文字」 | 目标程序不支持复制，或者按下热键时选中状态已经丢失 |
| Win11 记事本里取不到 | 记事本有未保存内容时会弹「是否保存」，挡住 Ctrl+A。先把对话框处理掉 |

引擎挂掉不需要重启程序，再按一次热键会自动拉起来。

## 实现要点

- 主程序只负责取词、画窗、转发 HTTP，模型跑在 `llama-server.exe` 子进程里
- 取词和翻译都在工作线程，主线程只收消息，按热键立刻弹窗
- 弹窗用 `WS_EX_NOACTIVATE`，不抢焦点，划词后源程序的选区不会丢
- 弹窗宽度按内容自适应（56–400 DIP），高度封顶在工作区 3/4，用完即销毁，不做隐藏常驻
- 单击弹窗＝整段复制。窗口是 `WS_EX_NOACTIVATE` 拿不到键盘焦点，拖选复制做不到，所以走一键全复制
- 长文本按句末标点切成 800 字一块逐块请求，每块单独算 `max_tokens`，所以长度上不封顶
- SSE 流式解析，16ms 合并一次刷新；UTF-8 跨包分块会在字符边界上切

## 第三方组件

| 组件 | 许可证 | 说明 |
|---|---|---|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) b11118 | MIT | `runtime/` 下的 `llama-server.exe` 与各 DLL |
| [libomp](https://openmp.llvm.org/) | Apache 2.0 with LLVM Exceptions | `runtime/libomp.dll`，许可见 `runtime/LICENSE-LLVM-OpenMP` |
| [Hy-MT2-1.8B](https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF) | Apache 2.0 (© 2026 Tencent) | 模型权重，不随本仓库分发 |
| Microsoft YaHei UI | 系统字体 | 界面文字，不打包不嵌入 |
