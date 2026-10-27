#pragma once
#include <string>

enum class Lang {
    English,
    Japanese,
    Chinese,   // 已经是中文，不需要翻
    Unknown,
};

// 只做英/日识别，规则靠 Unicode 区间，不引任何检测库：
//   有平假名/片假名 -> 日语（日文里必然带假名，纯汉字的日文极少见）
//   否则有 CJK 表意 -> 中文
//   否则有拉丁字母 -> 英语
Lang DetectLang(const std::wstring& text);

// 用于拼 prompt 的「英语 / 日语」
const wchar_t* LangName(Lang lang);
