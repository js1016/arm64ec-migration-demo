//=============================================================================
// TextUtil.cpp —— 日志排版辅助函数实现
//=============================================================================

#include "TextUtil.h"

namespace
{
    // 判断一个码位是否为东亚全角字符（在等宽字体下占 2 列）。
    // 这里只覆盖本程序实际会用到的区间，足够日志排版使用。
    bool IsFullWidth(wchar_t ch)
    {
        return (ch >= 0x1100 && ch <= 0x115F) || // 韩文字母
               (ch >= 0x2E80 && ch <= 0x303E) || // CJK 部首、日文标点
               (ch >= 0x3041 && ch <= 0x33FF) || // 假名、CJK 兼容字符
               (ch >= 0x3400 && ch <= 0x4DBF) || // CJK 扩展 A
               (ch >= 0x4E00 && ch <= 0x9FFF) || // CJK 统一表意文字
               (ch >= 0xA000 && ch <= 0xA4CF) || // 彝文
               (ch >= 0xAC00 && ch <= 0xD7A3) || // 韩文音节
               (ch >= 0xF900 && ch <= 0xFAFF) || // CJK 兼容表意文字
               (ch >= 0xFE30 && ch <= 0xFE6F) || // CJK 兼容形式
               (ch >= 0xFF00 && ch <= 0xFF60) || // 全角 ASCII
               (ch >= 0xFFE0 && ch <= 0xFFE6);   // 全角符号
    }
}

namespace TextUtil
{

size_t DisplayWidth(const std::wstring& text)
{
    size_t width = 0;
    for (const wchar_t ch : text)
    {
        width += IsFullWidth(ch) ? 2u : 1u;
    }
    return width;
}

std::wstring PadRight(const std::wstring& text, size_t columns)
{
    const size_t width = DisplayWidth(text);
    if (width >= columns)
    {
        return text;
    }
    return text + std::wstring(columns - width, L' ');
}

} // namespace TextUtil
