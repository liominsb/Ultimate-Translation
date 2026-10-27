#include "langdetect.h"

static bool IsKana(wchar_t c) {
    // 平假名 3040-309F，片假名 30A0-30FF，片假名扩展 31F0-31FF
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF);
}

static bool IsCjk(wchar_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
}

static bool IsLatin(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

Lang DetectLang(const std::wstring& text) {
    bool kana = false, cjk = false, latin = false;

    for (wchar_t c : text) {
        if (IsKana(c))       kana = true;
        else if (IsCjk(c))   cjk = true;
        else if (IsLatin(c)) latin = true;
    }

    // 假名优先级最高：日英混合的句子仍应判成日语
    if (kana) return Lang::Japanese;
    if (cjk) return Lang::Chinese;
    if (latin) return Lang::English;
    return Lang::Unknown;
}

const wchar_t* LangName(Lang lang) {
    switch (lang) {
        case Lang::English:  return L"英语";
        case Lang::Japanese: return L"日语";
        case Lang::Chinese:  return L"中文";
        default:             return L"";
    }
}
