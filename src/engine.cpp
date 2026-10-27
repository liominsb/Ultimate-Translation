#include "engine.h"
#include "logger.h"
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

HANDLE g_hProcess = nullptr;
EngineConfig g_cfg;

// ---------- 编码转换 ----------

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

// 返回缓冲区里最后一个「完整 UTF-8 字符」的结束位置。
// SSE 分块会把一个汉字劈成两半（3 字节跨两个 TCP 包），
// 直接整体转换会产生乱码，必须先切在字符边界上。
size_t Utf8CompleteLen(const std::string& buf) {
    size_t i = 0, last = 0;
    while (i < buf.size()) {
        unsigned char c = (unsigned char)buf[i];
        size_t len;
        if (c < 0x80)                    len = 1;
        else if ((c & 0xE0) == 0xC0)     len = 2;
        else if ((c & 0xF0) == 0xE0)     len = 3;
        else if ((c & 0xF8) == 0xF0)     len = 4;
        else { ++i; last = i; continue; }  // 非法字节，容错跳过

        if (i + len > buf.size()) break;   // 尾巴不完整，留到下一块
        i += len;
        last = i;
    }
    return last;
}

// ---------- 极简 JSON 处理 ----------
// 只处理 llama-server 固定返回格式，不引 JSON 库。

std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o;
}

// pos 指向字符串内容的第一个字节（引号之后），结束后 pos 停在闭合引号之后
bool ParseJsonString(const std::string& s, size_t& pos, std::string& out) {
    out.clear();
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }

        if (pos >= s.size()) return false;
        char e = s[pos++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '"': out += '"';  break;
            case '\\': out += '\\'; break;
            case '/': out += '/';  break;
            case 'u': {
                if (pos + 4 > s.size()) return false;
                unsigned cp = (unsigned)strtoul(s.substr(pos, 4).c_str(), nullptr, 16);
                pos += 4;
                // 代理对（生僻字/emoji 会出现）
                if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() &&
                    s[pos] == '\\' && s[pos + 1] == 'u') {
                    unsigned lo = (unsigned)strtoul(s.substr(pos + 2, 4).c_str(), nullptr, 16);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        pos += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                if (cp < 0x80) {
                    out += (char)cp;
                } else if (cp < 0x800) {
                    out += (char)(0xC0 | (cp >> 6));
                    out += (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += (char)(0xE0 | (cp >> 12));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                } else {
                    out += (char)(0xF0 | (cp >> 18));
                    out += (char)(0x80 | ((cp >> 12) & 0x3F));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += e; break;
        }
    }
    return false;
}

// 从一条 SSE 的 data 负载里取 choices[0].delta.content
std::string ExtractDelta(const std::string& line) {
    size_t d = line.find("\"delta\"");
    if (d == std::string::npos) return "";
    size_t c = line.find("\"content\"", d);
    if (c == std::string::npos) return "";

    // content 的值不一定是字符串：流式首帧是 {"role":"assistant","content":null}，
    // 只看冒号后面的字符是不是引号。早期版本无条件找下一个引号，
    // 结果把同一行的 "created" 字段名当成了译文，每句开头都多出一个 created。
    size_t v = c + 9;   // 跳过 "content" 这 9 个字符，落在冒号上
    while (v < line.size() && (line[v] == ':' || line[v] == ' ')) ++v;
    if (v >= line.size() || line[v] != '"') return "";

    size_t pos = v + 1;
    std::string val;
    if (!ParseJsonString(line, pos, val)) return "";
    return val;
}

// ---------- HTTP ----------

void CloseHandleSafe(HINTERNET& h) {
    if (h) { WinHttpCloseHandle(h); h = nullptr; }
}

bool HttpGetHealth() {
    bool ok = false;
    HINTERNET session = WinHttpOpen(L"TranslateBall/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    WinHttpSetTimeouts(session, 1000, 1000, 2000, 2000);

    HINTERNET conn = WinHttpConnect(session, L"127.0.0.1", (INTERNET_PORT)g_cfg.port, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", L"/health", nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (req) {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD code = 0, len = sizeof(code);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                    WINHTTP_NO_HEADER_INDEX);
                ok = (code == 200);
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

bool HttpPostStream(const std::string& body,
                    const std::function<void(const std::string&)>& onChunk,
                    std::wstring& error) {
    HINTERNET session = WinHttpOpen(L"TranslateBall/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = L"WinHTTP 初始化失败"; return false; }

    // 连接 5s / 发送 10s / 接收 60s，避免引擎卡死时永远转圈
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 60000);

    HINTERNET conn = WinHttpConnect(session, L"127.0.0.1", (INTERNET_PORT)g_cfg.port, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"POST", L"/v1/chat/completions", nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                         : nullptr;
    bool ok = false;

    if (req) {
        WinHttpAddRequestHeaders(req,
            L"Content-Type: application/json\r\nAccept: text/event-stream\r\n",
            (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
            error = L"发送请求失败（引擎可能未就绪）";
        } else if (!WinHttpReceiveResponse(req, nullptr)) {
            error = L"等待引擎响应超时";
        } else {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            if (code != 200) {
                error = L"引擎返回 HTTP " + std::to_wstring(code);
            } else {
                ok = true;
                std::vector<char> buf(8192);
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                    if (avail > buf.size()) buf.resize(avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
                    onChunk(std::string(buf.data(), read));
                }
            }
        }
    } else if (conn) {
        error = L"创建请求失败";
    } else {
        error = L"连接本地引擎失败，端口 " + std::to_wstring(g_cfg.port);
    }

    CloseHandleSafe(req);
    CloseHandleSafe(conn);
    CloseHandleSafe(session);
    return ok;
}

} // namespace

bool EngineStart(const EngineConfig& cfg) {
    g_cfg = cfg;

    if (g_hProcess) {
        // 句柄还在不等于真能用：可能是上次健康检查超时留下的僵尸进程。
        // 早期版本这里直接 return true，结果引擎挂过一次之后再也起不来。
        if (HttpGetHealth()) return true;
        Log(L"引擎进程存在但健康检查失败，先结束再重启");
        EngineStop();
    }

    if (cfg.modelPath.empty()) {
        Log(L"EngineStart 失败：模型路径为空");
        return false;
    }

    std::wstring exe = cfg.runtimeDir + L"\\llama-server.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"EngineStart 失败：找不到 " + exe);
        return false;
    }
    if (GetFileAttributesW(cfg.modelPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"EngineStart 失败：找不到模型 " + cfg.modelPath);
        return false;
    }

    wchar_t cmd[2048];
    swprintf(cmd, 2048,
             L"\"%ls\" -m \"%ls\" --host 127.0.0.1 --port %d "
             L"-ngl %d -fa on -t %d -c %d -b 128 -np 1 "
             L"--no-webui --no-warmup --no-slots",
             exe.c_str(), cfg.modelPath.c_str(), cfg.port,
             cfg.gpuLayers, cfg.threads, cfg.ctx);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    // CreateProcessW 要求命令行可写，不能直接传字面量
    std::vector<wchar_t> cmdBuf(cmd, cmd + wcslen(cmd) + 1);

    // 工作目录必须是 runtime 目录，否则 llama-server 找不到同级的 ggml-*.dll
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, cfg.runtimeDir.c_str(), &si, &pi)) {
        Log(L"EngineStart 失败：CreateProcessW 错误码 " + std::to_wstring(GetLastError()));
        return false;
    }

    CloseHandle(pi.hThread);
    g_hProcess = pi.hProcess;

    // 等就绪：模型加载实测 3 秒左右，给 60 秒余量
    for (int i = 0; i < 300; ++i) {
        if (WaitForSingleObject(g_hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(g_hProcess, &code);
            Log(L"引擎进程提前退出，退出码 " + std::to_wstring(code));
            CloseHandle(g_hProcess);
            g_hProcess = nullptr;
            return false;
        }
        if (HttpGetHealth()) return true;
        Sleep(200);
    }

    Log(L"EngineStart 失败：60 秒内未通过健康检查");
    return false;
}

void EngineStop() {
    if (!g_hProcess) return;
    TerminateProcess(g_hProcess, 0);
    WaitForSingleObject(g_hProcess, 3000);
    CloseHandle(g_hProcess);
    g_hProcess = nullptr;
}

bool EngineRunning() {
    return g_hProcess && WaitForSingleObject(g_hProcess, 0) != WAIT_OBJECT_0;
}

std::wstring TranslateText(const std::wstring& text,
                           const std::wstring& srcLang,
                           const std::wstring& tgtLang,
                           const std::function<void(const std::wstring&)>& onDelta,
                           std::wstring& error) {
    if (!EngineRunning()) {
        error = L"引擎未运行";
        return L"";
    }

    std::wstring prompt = L"将以下" + srcLang + L"文本翻译为" + tgtLang +
                          L"，注意只需要输出翻译后的结果，不要额外解释：\n\n" + text;

    char sampling[160];
    snprintf(sampling, sizeof(sampling),
             "\"}],\"temperature\":%.2f,\"top_p\":0.6,\"top_k\":20,"
             "\"repeat_penalty\":1.05,\"max_tokens\":256,\"stream\":true}",
             g_cfg.temperature);

    std::string body =
        "{\"model\":\"local\",\"messages\":[{\"role\":\"user\",\"content\":\"" +
        JsonEscape(WideToUtf8(prompt)) + sampling;

    std::wstring result;
    std::string pending;   // 还没凑成完整一行的 SSE
    std::string leftover;  // 还没凑成完整字符的 UTF-8 尾巴

    bool ok = HttpPostStream(body, [&](const std::string& chunk) {
        pending += chunk;

        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.rfind("data: ", 0) != 0) continue;
            std::string payload = line.substr(6);
            if (payload.empty() || payload == "[DONE]") continue;

            std::string delta = ExtractDelta(payload);
            if (delta.empty()) continue;

            leftover += delta;
            size_t safe = Utf8CompleteLen(leftover);
            if (safe == 0) continue;   // 整个字符还没收全，等下一块

            std::wstring piece = Utf8ToWide(leftover.substr(0, safe));
            leftover.erase(0, safe);
            result += piece;
            if (onDelta) onDelta(piece);
        }
    }, error);

    if (!ok && result.empty()) return L"";
    return result;
}
