# 终极翻译

Windows 划词翻译工具。选中英文或日文，按 `Alt+D`，鼠标旁弹出译文。

本地推理，不联网。模型是腾讯混元 Hy-MT2-1.8B（Q6_K 量化），通过 Vulkan 跑在显卡上。

> **这个仓库只有源码**。`runtime/`（推理运行时，70MB）和 `models/`（模型权重，1.4GB）体积太大没进版本库，需要自己准备，见下面「准备运行环境」一节。想直接用的话拿打包好的发行版，不用从头配。

## 用法

1. 双击 `TranslateBall.exe`，托盘出现图标，等 3 秒左右模型加载完成
2. 在任何程序里用鼠标选中英文/日文
3. 按 `Alt+D`
4. 弹窗出现在鼠标右下角，译文流式输出；点任意其它地方、切到别的窗口，或者 12 秒后自动消失

托盘图标右键有：翻译选中文字 / 中文翻译英文（勾选=中译英，不勾=英日译中）/ 重载翻译引擎 / 退出。

方向切换会记到 `config.ini`，下次启动保持。

## 目录结构

```
TranslateBall.exe      主程序（881KB，静态链接，无运行库依赖）
config.ini             配置（首次运行自动生成）
runtime/               llama-server 与所需 DLL（约 70MB，需自行下载）
models/                放 .gguf 模型（需自行下载）
```

`runtime/` 和 `models/` 必须和 exe 放在同一层，不能挪走。

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

## 准备运行环境

仓库里只有源码。`runtime/` 和 `models/` 体积太大，没进仓库，需要自己准备。

### 1. 推理运行时

下载 llama.cpp 官方的 Windows Vulkan 构建（b11118）：

<https://github.com/ggml-org/llama.cpp/releases/tag/b11118>

选 `llama-b11118-bin-win-vulkan-x64.zip`（约 30MB），解压后把里面的 `.exe` 和 `.dll` 全部放进 `runtime/`。

必须齐全：`llama-server.exe`、`llama-server-impl.dll`、`llama-common.dll`、`llama.dll`、`ggml.dll`、`ggml-base.dll`、`ggml-cpu-*.dll`、`ggml-vulkan.dll`、`mtmd.dll`、`libomp.dll`。

目标机器如果没装 VC++ 2015-2022 运行库，还要把 `api-ms-win-crt-*.dll`、`ucrtbase.dll`、`VCRUNTIME140*.dll` 一并放进去，否则启动只报一句 `error while loading shared libraries`，不会告诉你缺哪个。

### 2. 模型

从 ModelScope 下载（国内直连，比 HuggingFace 稳）：

<https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF>

推荐 `Hy-MT2-1.8B-Q6_K.gguf`（1.4GB），放进 `models/`。

单个文件的直链：

```
https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/resolve/master/Hy-MT2-1.8B-Q6_K.gguf
```

## 换模型

把别的 `.gguf` 丢进 `models/` 即可，程序会自动挑一个标准量化版本（Q4_K_M / Q5_K / Q6_K / Q8_0 优先）。放多个时挑中的是哪一个由文件名匹配决定，建议只留一个。

**注意**：官方的 `1.25bit` 和 `2bit` 版本不能用。它们依赖腾讯自研的 STQ kernel（llama.cpp PR #22836），还没并进主线，现有运行时会直接报 tensor offset 错误。实测加载失败的原文：

```
gguf_init_from_reader: tensor 'blk.0.attn_k_norm.weight' has offset ... expected ...
```

显存占用参考：Q6_K 约 1.8GB，Q8_0 约 2.4GB。8GB 显卡都够。

速度参考（i9-14900HX + RTX 4060 Laptop，Q6_K，Vulkan 全量 offload）：生成约 113 tok/s，单句端到端 0.07–0.2 秒。

## 已知限制

- 取词靠模拟 Ctrl+C，会短暂占用剪贴板（约 100ms），之后原样恢复
- 剪贴板里原本是图片/文件时，会被这次取词覆盖掉
- 选中的文字超过 500 字会拒绝翻译，避免超出 512 的上下文窗口
- 默认方向是英/日 → 中，选中文会提示切换；切到中译英后反过来
- 朗读功能未实现

## 排错

出问题先看 `logs/app.log`（UTF-8，超过 512KB 自动重建）。正常运行时不写任何内容，只在这几种情况下留痕：

- 引擎启动失败（模型缺失、DLL 缺失、端口被占、进程提前退出）
- 引擎进程崩溃后被自动拉起
- 翻译请求失败
- 翻译耗时超过 3 秒

想看完整过程（包括每次取词的内容）目前没有开关，需要的话可以临时加。

常见情况：

| 现象 | 原因 |
|---|---|
| 托盘气泡「引擎启动失败」 | 看日志里的具体行，通常是 models 目录没有 gguf、runtime 缺 DLL，或者端口被占 |
| 提示「正在启动翻译引擎」 | 引擎崩了在自动重启，等两秒再按一次热键即可 |
| 提示「没有取到选中的文字」 | 目标程序不支持复制，或者按下热键时选中状态已经丢失 |

引擎挂掉不需要重启程序，再按一次热键会自动拉起来。

## 实现要点

- 主程序只负责取词、画窗、转发 HTTP，模型跑在 `llama-server.exe` 子进程里，所以主程序私有内存只有 3.5MB 左右
- 弹窗用 `WS_EX_NOACTIVATE`，不抢焦点，划词后源程序的选区不会丢
- 窗口用完即销毁，不做隐藏常驻
- SSE 流式解析，逐字上屏；UTF-8 跨包分块会在字符边界上切
