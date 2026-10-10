/**
 *  SubtitleTextFit.h
 *
 *  字幕文字的折行与截断：把「说话人前缀 + 内容」按字幕宽度折成不超过最多行数的几行，放不下的部分
 *  在最后一行末尾截断并加省略号「…」。规则：
 *    · 中文、日文、韩文等逐字折行；连续的拉丁字母、数字与常见半角标点视为一个单词，单词不拆开，
 *      只有一个单词比整行还宽时才按字符拆；
 *    · 行首、行尾的空格去掉；
 *    · 说话人前缀（如「张三说：」）整体放在第一行、不拆开。名字特别长时先由 FitSpeakerPrefix 截短，
 *      保证前缀不超过一行宽度的一半，第一行总能看到一部分内容；
 *    · 群聊字幕在说话人前缀之前再放一段来源标注「【群名】」，同样整体放在第一行；群名过长时同样由
 *      FitSpeakerPrefix 截短，最多占一行宽度的四分之一；
 *    · 不拆开 UTF-16 代理对（emoji 等），省略号也不会插在代理对中间。
 *  文字宽度由调用方传入的测量函数给出，因此本模块不依赖任何字体与设备，可以完全用单元测试覆盖
 *  （见 Tests/SubtitleTests.cpp 的 SUBF 组）。
 *
 *  典型用法：
 *      std::wstring prefix = subtitle::FitSpeakerPrefix(TR(L"{0}说："), name, width / 2, measure, ctx);
 *      subtitle::FitResult r = subtitle::FitSubtitleText(prefix, content, width, maxLines, measure, ctx);
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <string>
#include <vector>

namespace subtitle {

/**
 *  测量文字宽度的回调。
 *    ctx： 调用方的上下文（如测量用的 GDI+ 对象），原样传回。
 *    text：文字起点，不要求以 NUL 结尾。
 *    len： 文字长度（UTF-16 单元数），可以为 0。
 *  返回：这段文字按字幕字体排出来的宽度，像素，不小于 0。
 */
typedef int (*MeasureTextFn)(void* ctx, const wchar_t* text, int len);

//省略号（U+2026）
const wchar_t kEllipsisChar = 0x2026;

//说话人前缀最多占一行宽度的百分比：超过时截短名字，保证第一行总能看到一部分内容
const int kSpeakerMaxWidthPercent = 50;
//群聊字幕行首的来源标注（「【群名】」）最多占一行宽度的百分比：超过时截短群名（用户 2026-10-05 确认）
const int kOriginMaxWidthPercent = 25;

//排好的一行
struct FitLine
{
    std::wstring m_text;        //这一行的文字（截断时已含末尾的省略号）
    int          m_originLen;   //这一行开头属于来源标注（「【群名】」）的字符数（UTF-16 单元）；只有第一行可能大于 0
    int          m_speakerLen;  //来源标注之后属于说话人前缀的字符数（UTF-16 单元）；只有第一行可能大于 0

    FitLine()
        : m_originLen(0)
        , m_speakerLen(0)
    {
    }
};

//折行结果
struct FitResult
{
    std::vector<FitLine> m_lines;      //排好的各行，至少有一行（前缀与内容都为空时是一个空行）
    bool                 m_truncated;  //内容是否因行数不够被截断（截断时最后一行末尾已加省略号）

    FitResult()
        : m_truncated(false)
    {
    }
};

/**
 *  按宽度把「说话人前缀 + 内容」折成不超过 maxLines 行。
 *    speakerPrefix：说话人前缀（已套好模板、已截短），可以为空（等待提示、撤回提示没有前缀）。
 *    content：      内容文字，调用方应已合并空白。
 *    maxWidthPx：   一行的最大宽度，像素；不大于 0 时按 1 处理（每行至少放一个字符）。
 *    maxLines：     最多行数；小于 1 时按 1 处理。
 *    measure / ctx：测量文字宽度的回调与它的上下文；measure 不能为 NULL。
 *  返回：折行结果。每行宽度不超过 maxWidthPx，只有单个字符就比一行还宽时例外（该行只放这一个字符）。
 */
FitResult FitSubtitleText(const std::wstring& speakerPrefix, const std::wstring& content, int maxWidthPx,
                          int maxLines, MeasureTextFn measure, void* ctx);

/**
 *  同上，第一行最前面再放一段来源标注（群聊字幕的「【群名】」）。来源标注与说话人前缀一起整体放在第一行、
 *  不拆开，排完后 m_originLen 与 m_speakerLen 分别给出两段的长度。
 *    originPrefix：来源标注（已套好模板、已截短，见 FitSpeakerPrefix），可以为空（单聊）。
 *    其余参数：    同上。
 *  返回：折行结果。
 */
FitResult FitSubtitleText(const std::wstring& originPrefix, const std::wstring& speakerPrefix,
                          const std::wstring& content, int maxWidthPx, int maxLines, MeasureTextFn measure, void* ctx);

/**
 *  把说话人名字套进模板，得到说话人前缀；前缀宽度超过 maxPx 时把名字从尾部截短并加省略号。
 *    pattern：     模板，含 {0} 占位符，如 TR(L"{0}说：") 的结果；为 NULL 时视为 L"{0}"。
 *    name：        说话人显示名。
 *    maxPx：       前缀的最大宽度，像素。
 *    measure / ctx：同 FitSubtitleText。
 *  返回：套好的前缀。名字只剩一个字符仍放不下时，返回名字只保留省略号的前缀。
 */
std::wstring FitSpeakerPrefix(const wchar_t* pattern, const std::wstring& name, int maxPx,
                              MeasureTextFn measure, void* ctx);

} // namespace subtitle
