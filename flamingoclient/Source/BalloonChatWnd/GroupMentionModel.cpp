/**
 *  GroupMentionModel.cpp
 *
 *  群聊「@提醒」纯逻辑部分的实现 —— UTF-8/UTF-16 偏移换算、发送前文本重扫、
 *  「是否@到我」判定。整体方案见 docs/群聊@提醒方案.md。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "GroupMentionModel.h"

#include <cstddef>

namespace balloonchatwnd {

namespace {

// UTF-8 首字节的分类阈值。单独命名避免散落的魔数：
// 0x80 以下是 ASCII 单字节；0xC0~0xDF 是 2 字节序列首字节；0xE0~0xEF 是 3 字节；
// 0xF0~0xF7 是 4 字节；0x80~0xBF 是续字节（不能作首字节）。
const unsigned char kUtf8AsciiMax        = 0x7F;   // ASCII 单字节上界
const unsigned char kUtf8ContinuationMin = 0x80;   // 续字节下界
const unsigned char kUtf8ContinuationMax = 0xBF;   // 续字节上界
const unsigned char kUtf8Lead2Min        = 0xC2;   // 2 字节序列首字节下界（C0/C1 是过长编码，非法）
const unsigned char kUtf8Lead2Max        = 0xDF;   // 2 字节序列首字节上界
const unsigned char kUtf8Lead3Min        = 0xE0;   // 3 字节序列首字节下界
const unsigned char kUtf8Lead3Max        = 0xEF;   // 3 字节序列首字节上界
const unsigned char kUtf8Lead4Min        = 0xF0;   // 4 字节序列首字节下界
const unsigned char kUtf8Lead4Max        = 0xF4;   // 4 字节序列首字节上界（F5 以上超出 U+10FFFF）

// 一个 BMP 内的码点占 1 个 UTF-16 单元；BMP 之外（4 字节 UTF-8）占 2 个（代理对）。
const int kUtf16UnitsBmp         = 1;
const int kUtf16UnitsSupplementary = 2;

/**
 *  取 utf8 中位置 pos 处这个 UTF-8 序列的字节长度。
 *    utf8：源串；pos：序列首字节位置（调用方保证 < size）。
 *  @return 1~4；pos 处不是合法首字节、或后续续字节不足 / 不合法时返回 0。
 */
int Utf8SequenceLength(const std::string& utf8, std::size_t pos)
{
    const unsigned char lead = static_cast<unsigned char>(utf8[pos]);

    int need = 0;
    if (lead <= kUtf8AsciiMax)
    {
        return 1;
    }
    else if (lead >= kUtf8Lead2Min && lead <= kUtf8Lead2Max)
    {
        need = 2;
    }
    else if (lead >= kUtf8Lead3Min && lead <= kUtf8Lead3Max)
    {
        need = 3;
    }
    else if (lead >= kUtf8Lead4Min && lead <= kUtf8Lead4Max)
    {
        need = 4;
    }
    else
    {
        return 0;   // 续字节或非法首字节
    }

    // 续字节数量与取值都要合法，否则整个序列判非法。
    if (pos + static_cast<std::size_t>(need) > utf8.size())
    {
        return 0;
    }
    for (int i = 1; i < need; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(utf8[pos + static_cast<std::size_t>(i)]);
        if (c < kUtf8ContinuationMin || c > kUtf8ContinuationMax)
        {
            return 0;
        }
    }
    return need;
}

/**
 *  判断 ch 是不是「可以作为 mention 起始位置前缀」的空白字符。
 *  只认 ASCII 空白 —— mention 前面出现全角空格的情形极少，且把全角空格
 *  当分隔反而会让 "……@张三" 这类文本产生歧义，故从严。
 */
bool IsAsciiSpace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

} // anonymous namespace

int Utf8ByteOffsetToUtf16Index(const std::string& utf8Text, int byteOff)
{
    if (byteOff < 0 || static_cast<std::size_t>(byteOff) > utf8Text.size())
    {
        return -1;
    }

    int         utf16Index = 0;
    std::size_t pos        = 0;
    const std::size_t target = static_cast<std::size_t>(byteOff);

    while (pos < target)
    {
        const int seqLen = Utf8SequenceLength(utf8Text, pos);
        if (seqLen == 0)
        {
            return -1;   // 正文含非法 UTF-8 序列
        }
        // 目标偏移落在这个序列<u>中间</u>：不是合法的字符边界。
        if (pos + static_cast<std::size_t>(seqLen) > target)
        {
            return -1;
        }
        utf16Index += (seqLen == 4) ? kUtf16UnitsSupplementary : kUtf16UnitsBmp;
        pos += static_cast<std::size_t>(seqLen);
    }

    return utf16Index;
}

bool CanMentionAll(int role)
{
    return role == kGroupRoleAdminVal || role == kGroupRoleOwnerVal;
}

std::vector<RenderSpan> ConvertSpansForRender(const std::string& utf8Text,
                                              const std::vector<MentionSpan>& spans,
                                              long long myUid)
{
    std::vector<RenderSpan> out;
    out.reserve(spans.size());

    for (std::size_t i = 0; i < spans.size(); ++i)
    {
        const MentionSpan& src = spans[i];
        if (src.m_len <= 0)
        {
            continue;   // 长度非正的片段无意义，丢弃
        }

        const int beginIdx = Utf8ByteOffsetToUtf16Index(utf8Text, src.m_off);
        if (beginIdx < 0)
        {
            continue;   // 起点越界 / 不在字符边界
        }
        const int endIdx = Utf8ByteOffsetToUtf16Index(utf8Text, src.m_off + src.m_len);
        if (endIdx < 0 || endIdx <= beginIdx)
        {
            continue;   // 终点越界 / 不在字符边界 / 区间为空
        }

        RenderSpan dst;
        dst.m_uid = src.m_uid;
        dst.m_off = beginIdx;
        dst.m_len = endIdx - beginIdx;
        // 未登录 / 未知本人 uid 时一律按"不是@我"处理，避免误标成醒目色。
        // 逐片段独立判定：这一段 @ 的是我本人，或者它就是「@所有人」那一段。
        if (myUid == 0)
        {
            dst.m_isMe = false;
        }
        else
        {
            dst.m_isMe = (src.m_uid == myUid) || (src.m_uid == kMentionUidAll);
        }
        out.push_back(dst);
    }

    return out;
}

std::vector<MentionSpan> RescanMentions(const std::string& utf8Text,
                                        const std::vector<MentionCandidate>& candidates)
{
    std::vector<MentionSpan> out;
    if (utf8Text.empty() || candidates.empty())
    {
        return out;
    }

    // 每条候选只能被消费一次：群里有同名成员时，文本里第 N 次出现该名字归第 N 条同名候选。
    std::vector<bool> used(candidates.size(), false);

    std::size_t pos = 0;
    while (pos < utf8Text.size())
    {
        if (utf8Text[pos] != '@')
        {
            ++pos;
            continue;
        }

        // 只有位于文本开头、或紧跟在空白之后的 '@' 才可能是 mention ——
        // 借此把 "abc@example.com" 这类邮箱排除掉。
        if (pos > 0 && !IsAsciiSpace(utf8Text[pos - 1]))
        {
            ++pos;
            continue;
        }

        // 在本位置找显示名最长的那条未用候选（避免 "张三" 抢走 "张三丰" 的位置）。
        std::size_t bestIdx = candidates.size();
        std::size_t bestLen = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (used[i])
            {
                continue;
            }
            const std::string& name = candidates[i].m_displayName;
            if (name.empty())
            {
                continue;
            }
            // 文本里 '@' 之后必须原样跟着显示名。
            if (utf8Text.compare(pos + 1, name.size(), name) != 0)
            {
                continue;
            }
            if (name.size() > bestLen)
            {
                bestLen = name.size();
                bestIdx = i;
            }
        }

        if (bestIdx == candidates.size())
        {
            ++pos;   // 这个 '@' 后面不是任何候选的名字，跳过
            continue;
        }

        MentionSpan span;
        span.m_uid = candidates[bestIdx].m_uid;
        span.m_off = static_cast<int>(pos);
        span.m_len = static_cast<int>(1 + bestLen);   // '@' 计入片段，尾随空格不计入
        out.push_back(span);

        used[bestIdx] = true;
        pos += static_cast<std::size_t>(span.m_len);
    }

    return out;
}

bool IsMentioningMe(const std::vector<MentionSpan>& spans, bool atAll, long long myUid)
{
    if (myUid == 0)
    {
        return false;   // 未登录 / 未知本人 uid，无从判定
    }
    if (atAll)
    {
        return true;
    }
    for (std::size_t i = 0; i < spans.size(); ++i)
    {
        // 本人 uid 命中，或正文里确实有一段「@所有人」—— 后者是防御性的：正常发送方
        // 会同时置 atAll，但不依赖对端一定把标志位写对。
        if (spans[i].m_uid == myUid || spans[i].m_uid == kMentionUidAll)
        {
            return true;
        }
    }
    return false;
}

std::vector<RenderSpan> BuildRenderSpansForMessage(const std::string& utf8Text,
                                                   const std::vector<MentionSpan>& spans,
                                                   bool atAll,
                                                   long long myUid)
{
    // 本条压根没带 @ 的信息：直接给空，让气泡走「不含 @」的渲染快路径。
    // 注意 atAll 为真但 spans 为空也要往下走 —— 老版本 / 异常发送方可能只置了
    // 标志位没写片段，此时 ConvertSpansForRender 自然产出空，结果一样，但这里
    // 不额外做判断，把「什么算带 @」这件事只留一处描述。
    if (spans.empty() && !atAll)
    {
        return std::vector<RenderSpan>();
    }
    return ConvertSpansForRender(utf8Text, spans, myUid);
}

bool HasMentionAll(const std::vector<MentionSpan>& spans)
{
    for (std::size_t i = 0; i < spans.size(); ++i)
    {
        if (spans[i].m_uid == kMentionUidAll)
        {
            return true;
        }
    }
    return false;
}

} // namespace balloonchatwnd
