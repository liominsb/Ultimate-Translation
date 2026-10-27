"""对本地 llama-server 跑一组固定测试句，用于横向比较不同量化模型。

用法: python tools/eval_translate.py <端口> <模型名>
输出: 每句的原文/译文/耗时，末尾给平均生成速度。
"""
import json
import sys
import time
import urllib.request

CASES = [
    ("en", "The quick brown fox jumps over the lazy dog."),
    ("en", "Could not establish a connection to the database server after 3 retries."),
    ("en", "This function returns a pointer to the newly allocated buffer; "
           "the caller is responsible for freeing it."),
    ("en", "Note that the cache is invalidated whenever the underlying configuration changes."),
    ("en", "We recommend enabling hardware acceleration to reduce latency."),
    ("en", "Freedom is never free."),
    ("ja", "昨日は雨が降っていたので、家でゆっくり本を読んでいました。"),
    ("ja", "この関数は新しく確保されたバッファへのポインタを返します。"),
    ("ja", "設定を変更した場合は、アプリケーションを再起動してください。"),
    ("ja", "彼女は毎朝6時に起きて、公園を散歩してから会社に行きます。"),
    ("ja", "エラーが発生しました。もう一度お試しください。"),
    ("ja", "自由は決して無料ではない。"),
]

LANG_NAME = {"en": "英语", "ja": "日语"}


def translate(port, lang, text):
    prompt = ("将以下%s文本翻译为中文，注意只需要输出翻译后的结果，不要额外解释：\n\n%s"
              % (LANG_NAME[lang], text))
    body = {
        "model": "local",
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.7,
        "top_p": 0.6,
        "top_k": 20,
        "repeat_penalty": 1.05,
        "max_tokens": 256,
        "stream": False,
    }
    req = urllib.request.Request(
        "http://127.0.0.1:%d/v1/chat/completions" % port,
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=180) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    dt = time.time() - t0
    out = data["choices"][0]["message"]["content"].strip()
    tps = data.get("timings", {}).get("predicted_per_second", 0.0)
    return out, dt, tps


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 58199
    name = sys.argv[2] if len(sys.argv) > 2 else "model"
    print("# %s (port %d)\n" % (name, port))
    speeds = []
    total = 0.0
    for i, (lang, text) in enumerate(CASES, 1):
        try:
            out, dt, tps = translate(port, lang, text)
        except Exception as exc:
            print("%02d [%s] %s\n    ERROR: %s\n" % (i, lang, text, exc))
            continue
        speeds.append(tps)
        total += dt
        print("%02d [%s] %s" % (i, lang, text))
        print("   -> %s" % out.replace("\n", " / "))
        print("   (%.2fs, %.1f tok/s)\n" % (dt, tps))
    if speeds:
        print("平均生成速度: %.1f tok/s, 总耗时 %.1fs" % (sum(speeds) / len(speeds), total))


if __name__ == "__main__":
    main()
