# 终极翻译

Windows 划词翻译工具。选中英文或日文，按 `Alt+D`，鼠标旁弹出译文。

本地推理，不联网。模型是腾讯混元 Hy-MT2-1.8B（Q6_K 量化），通过 Vulkan 跑在显卡上。

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
2. 在任何程序里用鼠标选中英文/日文
3. 按 `Alt+D`
4. 弹窗出现在鼠标右下角，译文流式输出；点任意其它地方、切到别的窗口，或者 12 秒后自动消失

托盘图标右键有：翻译选中文字 / 中文翻译英文（勾选=中译英，不勾=英日译中）/ 重载翻译引擎 / 退出。

方向切换会记到 `config.ini`，下次启动保持。

## 配置

改完 `config.ini` 需要重启程序。

| 项 | 默认 | 说明 |
|---|---|---|
| `hotkey` | `Alt+D` | 支持 `Ctrl+Shift+X`、`F1`~`F24` 这类写法 |
| `zh2en` | `0` | `0` = 英/日 → 中；`1` = 中 → 英。托盘菜单那个勾选项改的就是它 |
| `port` | `58088` | 引擎监听端口，被占用就换一个 |
| `temperature` | `0.70` | 官方推荐值，想要更稳定的输出可以降到 0.3 |
| `max_tokens` | `256` | 划词单句足够，别调太大 |

目标语言不直接写中文，用 `zh2en` 开关表示——Windows 的 ini API 按系统 ANSI 码页写文件，中文值用 UTF-8 编辑器打开就是乱码。

## 换模型

把别的 `.gguf` 丢进 `models/` 即可，程序会自动挑一个标准量化版本（Q4_K_M / Q5_K / Q6_K / Q8_0 优先）。放多个时挑中哪个由文件名匹配决定，建议只留一个。

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
- 选中的文字超过 500 字会拒绝翻译，避免超出 512 的上下文窗口
- 默认方向是英/日 → 中，选中文会提示切换；切到中译英后反过来
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
- 弹窗宽度按内容自适应（100–500），用完即销毁，不做隐藏常驻
- SSE 流式解析，16ms 合并一次刷新；UTF-8 跨包分块会在字符边界上切

## 第三方组件

| 组件 | 许可证 | 说明 |
|---|---|---|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) b11118 | MIT | `runtime/` 下的 `llama-server.exe` 与各 DLL |
| [libomp](https://openmp.llvm.org/) | Apache 2.0 with LLVM Exceptions | `runtime/libomp.dll`，许可见 `runtime/LICENSE-LLVM-OpenMP` |
| [Hy-MT2-1.8B](https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF) | Apache 2.0 (© 2026 Tencent) | 模型权重，不随本仓库分发 |
| Microsoft YaHei UI | 系统字体 | 界面文字，不打包不嵌入 |
