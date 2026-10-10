/**
 *  敏感词匹配引擎的实现，SensitiveWordMatcher.cpp
 *
 *  规则说明见 SensitiveWordMatcher.h；归一化规则的取舍见 docs/敏感词方案.md 第 4.2 节。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "SensitiveWordMatcher.h"

#include <algorithm>

namespace chatserver {

namespace {

// ---- UTF-8 编解码用到的常量 ----

// 单字节（ASCII）字符的上界（不含）。
const uint32_t kAsciiLimit = 0x80;

// 首字节判定：两字节、三字节、四字节序列首字节的取值范围（含两端）。0xC0、0xC1 只能编出
// 超长形式，0xF5 及以上超出 Unicode 范围，都不是合法首字节。
const unsigned char kLead2First = 0xC2;
const unsigned char kLead2Last  = 0xDF;
const unsigned char kLead3First = 0xE0;
const unsigned char kLead3Last  = 0xEF;
const unsigned char kLead4First = 0xF0;
const unsigned char kLead4Last  = 0xF4;

// 后续字节的形式 10xxxxxx：与掩码相与后应等于标志值；取有效位用低 6 位掩码。
const unsigned char kContinuationMask  = 0xC0;
const unsigned char kContinuationValue = 0x80;
const unsigned char kContinuationBits  = 0x3F;

// 后续字节一般的取值范围（含两端）。
const unsigned char kContinuationFirst = 0x80;
const unsigned char kContinuationLast  = 0xBF;

// 三字节与四字节序列里，第二个字节须收窄范围的几种首字节及其第二字节的合法范围：
//   E0 后只能跟 A0～BF（否则是超长形式）；ED 后只能跟 80～9F（否则是代理区 U+D800～U+DFFF）；
//   F0 后只能跟 90～BF（否则是超长形式）；F4 后只能跟 80～8F（否则超出 U+10FFFF）。
const unsigned char kLeadE0 = 0xE0;
const unsigned char kLeadED = 0xED;
const unsigned char kLeadF0 = 0xF0;
const unsigned char kLeadF4 = 0xF4;
const unsigned char kAfterE0First = 0xA0;
const unsigned char kAfterEDLast  = 0x9F;
const unsigned char kAfterF0First = 0x90;
const unsigned char kAfterF4Last  = 0x8F;

// 首字节里有效位的掩码：两字节取低 5 位、三字节取低 4 位、四字节取低 3 位。
const unsigned char kLead2Bits = 0x1F;
const unsigned char kLead3Bits = 0x0F;
const unsigned char kLead4Bits = 0x07;

// 编码时各长度序列的码点上界（不含）与首字节标志。
const uint32_t kTwoByteLimit   = 0x800;
const uint32_t kThreeByteLimit = 0x10000;
const unsigned char kLead2Flag = 0xC0;
const unsigned char kLead3Flag = 0xE0;
const unsigned char kLead4Flag = 0xF0;

// 后续字节每个承载 6 位。
const int kContinuationShift = 6;

// 非法字节在解码结果里的替代字符 U+FFFD。它落在下面的忽略区间里，不影响匹配；摘要里按它输出，
// 保证结果是合法的 UTF-8。
const uint32_t kReplacementChar = 0xFFFD;

// ---- 归一化用到的码点 ----

// 全角 ASCII 区 U+FF01～U+FF5E 与半角 U+0021～U+007E 一一对应，相差这个偏移。
const uint32_t kFullwidthFirst  = 0xFF01;
const uint32_t kFullwidthLast   = 0xFF5E;
const uint32_t kFullwidthOffset = 0xFEE0;

// 全角空格，归一化时视为半角空格（随后作为空白被忽略）。
const uint32_t kIdeographicSpace = 0x3000;
const uint32_t kAsciiSpace       = 0x20;

// 表情标记 /f["数字"] 里用到的几个 ASCII 字符。
const uint32_t kSlash        = '/';
const uint32_t kFaceLetter   = 'f';
const uint32_t kOpenBracket  = '[';
const uint32_t kCloseBracket = ']';
const uint32_t kQuote        = '"';

// 表情标记里数字之前的固定前缀 /f[" 的长度。
const size_t kFaceTagPrefixLength = 4;

// 省略号 "…"（U+2026），摘要两端被截断时加上。
const uint32_t kEllipsis = 0x2026;

//一段闭区间的码点范围。
struct CodepointRange
{
    uint32_t m_first;   //区间起点（含）
    uint32_t m_last;    //区间终点（含）
};

// 归一化时忽略的非 ASCII 码点区间（ASCII 部分另行判断：字母数字以外的全部忽略）。
// 这些区间都是空白、标点、各类符号、表情或格式控制字符，词条与文字里出现时一律去掉，
// 使「敏*感」「敏😀感」与「敏感」视为相同。调整区间后，各企业的匹配器须整体重建。
const CodepointRange kIgnoredRanges[] =
{
    { 0x0080, 0x00BF },    //C1 控制字符、不换行空格、Latin-1 标点与符号（¡ © « ° ± · » ¿ 等）
    { 0x00D7, 0x00D7 },    //乘号 ×
    { 0x00F7, 0x00F7 },    //除号 ÷
    { 0x2000, 0x206F },    //通用标点：各种宽度的空格、零宽字符、连字符、引号、省略号、项目符号等
    { 0x20A0, 0x20CF },    //货币符号
    { 0x2100, 0x2BFF },    //字母式符号、数字形式、箭头、数学运算符、技术符号、制表符、几何图形、杂项符号、装饰符号等
    { 0x3000, 0x3004 },    //CJK 符号与标点：全角空格、、。〃〄
    { 0x3008, 0x303F },    //CJK 符号与标点：括号类〈〉《》「」『』【】、〒〓、波浪线等（跳过 々〆〇 三个字）
    { 0xFE00, 0xFE0F },    //变体选择符（emoji 后常跟的 FE0F 等）
    { 0xFE10, 0xFE1F },    //竖排标点
    { 0xFE30, 0xFE6F },    //CJK 兼容形式与小写变体标点
    { 0xFEFF, 0xFEFF },    //字节序标记 / 零宽不换行空格
    { 0xFF5F, 0xFF65 },    //半角 CJK 标点（全角 ASCII 已先映射为半角，不在此列）
    { 0xFFF0, 0xFFFF },    //特殊字符，含替代字符 U+FFFD（非法字节解码成它）
    { 0x1F000, 0x1FBFF },  //麻将、扑克、带圈字母数字、emoji、符号与象形文字、交通与地图符号、补充符号等
    { 0xE0000, 0xE007F },  //标签字符（旗帜类 emoji 的组成部分）
};

// 去掉词条首尾空白时认作空白的非 ASCII 码点区间（ASCII 部分取 0x20 及以下与 0x7F）。
const CodepointRange kTrimRanges[] =
{
    { 0x00A0, 0x00A0 },    //不换行空格
    { 0x2000, 0x200B },    //各种宽度的空格与零宽空格
    { 0x2028, 0x2029 },    //行分隔符、段分隔符
    { 0x202F, 0x202F },    //窄不换行空格
    { 0x205F, 0x205F },    //数学中等空格
    { 0x3000, 0x3000 },    //全角空格
    { 0xFEFF, 0xFEFF },    //零宽不换行空格
};

// ASCII 删除符，去首尾空白时与控制字符一并去掉。
const uint32_t kAsciiDelete = 0x7F;

// 判断码点是否落在某张区间表里。
bool InRanges(uint32_t cp, const CodepointRange* ranges, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (cp >= ranges[i].m_first && cp <= ranges[i].m_last)
            return true;
    }
    return false;
}

// 判断一个字节是否是后续字节，且落在 [first, last] 之内。
bool IsContinuation(unsigned char b, unsigned char first, unsigned char last)
{
    return (b & kContinuationMask) == kContinuationValue && b >= first && b <= last;
}

// 把 UTF-8 解码成码点序列。非法字节（孤立的后续字节、非法首字节、被截断或形式非法的序列的
// 首字节）各自解码成一个 U+FFFD，并只前进一个字节，后面的字节重新判断。
void DecodeUtf8(const std::string& s, std::vector<uint32_t>& out)
{
    out.clear();
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n)
    {
        const unsigned char b0 = static_cast<unsigned char>(s[i]);
        if (b0 < kAsciiLimit)
        {
            out.push_back(b0);
            ++i;
            continue;
        }

        //两字节序列
        if (b0 >= kLead2First && b0 <= kLead2Last)
        {
            if (i + 1 < n && IsContinuation(static_cast<unsigned char>(s[i + 1]),
                                            kContinuationFirst, kContinuationLast))
            {
                const uint32_t cp = (static_cast<uint32_t>(b0 & kLead2Bits) << kContinuationShift)
                                  | (static_cast<unsigned char>(s[i + 1]) & kContinuationBits);
                out.push_back(cp);
                i += 2;
                continue;
            }
        }
        //三字节序列：E0 与 ED 的第二字节范围另有限制
        else if (b0 >= kLead3First && b0 <= kLead3Last)
        {
            const unsigned char secondFirst = (b0 == kLeadE0) ? kAfterE0First : kContinuationFirst;
            const unsigned char secondLast  = (b0 == kLeadED) ? kAfterEDLast : kContinuationLast;
            if (i + 2 < n
                && IsContinuation(static_cast<unsigned char>(s[i + 1]), secondFirst, secondLast)
                && IsContinuation(static_cast<unsigned char>(s[i + 2]), kContinuationFirst, kContinuationLast))
            {
                const uint32_t cp = (static_cast<uint32_t>(b0 & kLead3Bits) << (2 * kContinuationShift))
                                  | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & kContinuationBits)
                                     << kContinuationShift)
                                  | (static_cast<unsigned char>(s[i + 2]) & kContinuationBits);
                out.push_back(cp);
                i += 3;
                continue;
            }
        }
        //四字节序列：F0 与 F4 的第二字节范围另有限制
        else if (b0 >= kLead4First && b0 <= kLead4Last)
        {
            const unsigned char secondFirst = (b0 == kLeadF0) ? kAfterF0First : kContinuationFirst;
            const unsigned char secondLast  = (b0 == kLeadF4) ? kAfterF4Last : kContinuationLast;
            if (i + 3 < n
                && IsContinuation(static_cast<unsigned char>(s[i + 1]), secondFirst, secondLast)
                && IsContinuation(static_cast<unsigned char>(s[i + 2]), kContinuationFirst, kContinuationLast)
                && IsContinuation(static_cast<unsigned char>(s[i + 3]), kContinuationFirst, kContinuationLast))
            {
                const uint32_t cp = (static_cast<uint32_t>(b0 & kLead4Bits) << (3 * kContinuationShift))
                                  | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & kContinuationBits)
                                     << (2 * kContinuationShift))
                                  | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 2]) & kContinuationBits)
                                     << kContinuationShift)
                                  | (static_cast<unsigned char>(s[i + 3]) & kContinuationBits);
                out.push_back(cp);
                i += 4;
                continue;
            }
        }

        //其余情况都是非法字节：记一个替代字符，只前进一个字节
        out.push_back(kReplacementChar);
        ++i;
    }
}

// 把一段码点编码成 UTF-8 追加到 out。调用方保证码点不超出 Unicode 范围。
void AppendUtf8(uint32_t cp, std::string& out)
{
    if (cp < kAsciiLimit)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < kTwoByteLimit)
    {
        out += static_cast<char>(kLead2Flag | (cp >> kContinuationShift));
        out += static_cast<char>(kContinuationValue | (cp & kContinuationBits));
    }
    else if (cp < kThreeByteLimit)
    {
        out += static_cast<char>(kLead3Flag | (cp >> (2 * kContinuationShift)));
        out += static_cast<char>(kContinuationValue | ((cp >> kContinuationShift) & kContinuationBits));
        out += static_cast<char>(kContinuationValue | (cp & kContinuationBits));
    }
    else
    {
        out += static_cast<char>(kLead4Flag | (cp >> (3 * kContinuationShift)));
        out += static_cast<char>(kContinuationValue | ((cp >> (2 * kContinuationShift)) & kContinuationBits));
        out += static_cast<char>(kContinuationValue | ((cp >> kContinuationShift) & kContinuationBits));
        out += static_cast<char>(kContinuationValue | (cp & kContinuationBits));
    }
}

// 判断从 pos 开始是否是一个完整的表情标记 /f["数字"]（与客户端 BalloonChatMsgFormat.cpp 的
// TryParseFaceTag 规则一致：数字至少一位，且必须以 "] 结尾）。
// @return 是表情标记时返回它占的字符数，否则返回 0
size_t FaceTagLength(const std::vector<uint32_t>& chars, size_t pos)
{
    const size_t n = chars.size();
    if (pos + kFaceTagPrefixLength >= n)
        return 0;
    if (chars[pos] != kSlash || chars[pos + 1] != kFaceLetter
        || chars[pos + 2] != kOpenBracket || chars[pos + 3] != kQuote)
        return 0;

    size_t i = pos + kFaceTagPrefixLength;
    const size_t digitsBegin = i;
    while (i < n && chars[i] >= '0' && chars[i] <= '9')
        ++i;
    if (i == digitsBegin)
        return 0;
    //数字之后必须紧跟 "]
    if (i + 1 >= n || chars[i] != kQuote || chars[i + 1] != kCloseBracket)
        return 0;
    return i + 2 - pos;
}

// 全角转半角、ASCII 大写转小写。
uint32_t MapCodepoint(uint32_t cp)
{
    if (cp >= kFullwidthFirst && cp <= kFullwidthLast)
        cp -= kFullwidthOffset;
    else if (cp == kIdeographicSpace)
        cp = kAsciiSpace;

    if (cp >= 'A' && cp <= 'Z')
        cp = cp - 'A' + 'a';
    return cp;
}

// 归一化时是否忽略这个码点（调用方须已经过 MapCodepoint）。
bool IsIgnoredCodepoint(uint32_t cp)
{
    //ASCII：只保留小写字母与数字（大写已转为小写）
    if (cp < kAsciiLimit)
        return !((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9'));
    return InRanges(cp, kIgnoredRanges, sizeof(kIgnoredRanges) / sizeof(kIgnoredRanges[0]));
}

// 去词条首尾空白时是否把这个码点当作空白。
bool IsTrimmableSpace(uint32_t cp)
{
    if (cp <= kAsciiSpace || cp == kAsciiDelete)
        return true;
    return InRanges(cp, kTrimRanges, sizeof(kTrimRanges) / sizeof(kTrimRanges[0]));
}

// 按归一化后的码点序列给词条下标排字典序，供 Build 一次建出字典树。
struct NormalizedLess
{
    const std::vector<std::vector<uint32_t> >* m_normalized;   //各词条归一化后的码点序列（借用）

    explicit NormalizedLess(const std::vector<std::vector<uint32_t> >* normalized)
        : m_normalized(normalized)
    {
    }

    bool operator()(size_t a, size_t b) const
    {
        const std::vector<uint32_t>& x = (*m_normalized)[a];
        const std::vector<uint32_t>& y = (*m_normalized)[b];
        return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
    }
};

// 建树时临时记下的一条边：父节点、码点、子节点。
struct TrieEdge
{
    int      m_parent;   //父节点编号
    uint32_t m_cp;       //边上的码点
    int      m_child;    //子节点编号
};

} // anonymous namespace

SensitiveWordMatcher::SensitiveWordMatcher()
{
    //空匹配器也保留一个根节点，Match 时不必特判
    m_fail.push_back(0);
    m_nodeWord.push_back(-1);
    m_output.push_back(-1);
    m_firstEdge.push_back(0);
    m_edgeCount.push_back(0);
}

void SensitiveWordMatcher::Normalize(const std::string& utf8, std::vector<uint32_t>& codepoints,
                                     std::vector<uint32_t>* origCharIndex)
{
    codepoints.clear();
    if (origCharIndex != NULL)
        origCharIndex->clear();

    std::vector<uint32_t> chars;
    DecodeUtf8(utf8, chars);
    codepoints.reserve(chars.size());

    size_t i = 0;
    while (i < chars.size())
    {
        if (chars[i] == kSlash)
        {
            //被转义的表情标记 //f["数字"]：两个斜杠都是符号、本来就会被忽略，跳过它们后，
            //其后的 f["数字"] 按普通文字处理
            if (i + 1 < chars.size() && chars[i + 1] == kSlash && FaceTagLength(chars, i + 1) > 0)
            {
                i += 2;
                continue;
            }
            //真正的表情标记：整个去掉
            const size_t tagLength = FaceTagLength(chars, i);
            if (tagLength > 0)
            {
                i += tagLength;
                continue;
            }
        }

        const uint32_t cp = MapCodepoint(chars[i]);
        if (!IsIgnoredCodepoint(cp))
        {
            codepoints.push_back(cp);
            if (origCharIndex != NULL)
                origCharIndex->push_back(static_cast<uint32_t>(i));
        }
        ++i;
    }
}

int SensitiveWordMatcher::CheckWord(const std::string& utf8, std::string* normalizedUtf8)
{
    if (normalizedUtf8 != NULL)
        normalizedUtf8->clear();

    //首尾空白在归一化时本来就会被忽略，所以对整个原文归一化与对去掉首尾空白后的部分
    //归一化，结果相同
    std::vector<uint32_t> normalized;
    Normalize(utf8, normalized, NULL);
    if (normalized.empty())
        return sensitive_word_check_empty;

    //长度按去掉首尾空白后的字符数计
    std::vector<uint32_t> chars;
    DecodeUtf8(utf8, chars);
    size_t first = 0;
    size_t last = chars.size();
    while (first < last && IsTrimmableSpace(chars[first]))
        ++first;
    while (last > first && IsTrimmableSpace(chars[last - 1]))
        --last;
    if (last - first > static_cast<size_t>(SENSITIVE_WORD_MAX_CHARS))
        return sensitive_word_check_too_long;

    if (normalizedUtf8 != NULL)
    {
        for (size_t i = 0; i < normalized.size(); ++i)
            AppendUtf8(normalized[i], *normalizedUtf8);
    }
    return sensitive_word_check_ok;
}

uint32_t SensitiveWordMatcher::CountChars(const std::string& utf8)
{
    std::vector<uint32_t> chars;
    DecodeUtf8(utf8, chars);
    return static_cast<uint32_t>(chars.size());
}

size_t SensitiveWordMatcher::Build(const std::vector<SensitiveWordEntry>& words)
{
    //先把全部词条归一化，归一化后为空的跳过
    std::vector<std::vector<uint32_t> > normalized(words.size());
    std::vector<size_t> order;
    order.reserve(words.size());
    size_t totalChars = 0;
    for (size_t i = 0; i < words.size(); ++i)
    {
        Normalize(words[i].m_word, normalized[i], NULL);
        if (normalized[i].empty())
            continue;
        order.push_back(i);
        totalChars += normalized[i].size();
    }

    //按归一化结果排字典序。稳定排序保证归一化后相同的词条里，输入中先出现的排在前面
    std::stable_sort(order.begin(), order.end(), NormalizedLess(&normalized));

    //一次建出字典树：排序后每个词条与前一个词条的公共前缀已经在树里，只需从公共前缀的
    //末端往下补新节点。同一父节点的子节点因此按码点升序依次创建，不必查找与插入
    m_fail.assign(1, 0);
    m_nodeWord.assign(1, -1);
    m_output.assign(1, -1);
    m_words.clear();
    m_fail.reserve(totalChars + 1);
    m_nodeWord.reserve(totalChars + 1);
    std::vector<TrieEdge> edges;
    edges.reserve(totalChars);

    std::vector<int> path(1, 0);        //上一个词条从根到词尾经过的节点，path[d] 为深度 d 的节点
    const std::vector<uint32_t>* prev = NULL;
    for (size_t k = 0; k < order.size(); ++k)
    {
        const size_t idx = order[k];
        const std::vector<uint32_t>& cps = normalized[idx];

        size_t common = 0;
        if (prev != NULL)
        {
            while (common < cps.size() && common < prev->size() && cps[common] == (*prev)[common])
                ++common;
        }
        path.resize(common + 1);
        int node = path[common];
        for (size_t d = common; d < cps.size(); ++d)
        {
            const int child = static_cast<int>(m_fail.size());
            m_fail.push_back(0);
            m_nodeWord.push_back(-1);
            TrieEdge e;
            e.m_parent = node;
            e.m_cp = cps[d];
            e.m_child = child;
            edges.push_back(e);
            path.push_back(child);
            node = child;
        }
        prev = &cps;

        //归一化后相同的词条只保留先出现的那一个（它在稳定排序后排在前面）
        if (m_nodeWord[node] >= 0)
            continue;

        WordInfo info;
        info.m_id = words[idx].m_id;
        info.m_category = words[idx].m_category;
        info.m_length = static_cast<uint32_t>(cps.size());
        m_nodeWord[node] = static_cast<int>(m_words.size());
        m_words.push_back(info);
    }

    //把边按父节点归并成连续数组（计数排序；同一父节点的边保持创建时的码点升序）
    const size_t nodeCount = m_fail.size();
    m_edgeCount.assign(nodeCount, 0);
    for (size_t i = 0; i < edges.size(); ++i)
        ++m_edgeCount[edges[i].m_parent];
    m_firstEdge.assign(nodeCount, 0);
    uint32_t running = 0;
    for (size_t n = 0; n < nodeCount; ++n)
    {
        m_firstEdge[n] = running;
        running += m_edgeCount[n];
    }
    m_edgeCp.assign(edges.size(), 0);
    m_edgeTarget.assign(edges.size(), 0);
    std::vector<uint32_t> fill(m_firstEdge);
    for (size_t i = 0; i < edges.size(); ++i)
    {
        const uint32_t slot = fill[edges[i].m_parent]++;
        m_edgeCp[slot] = edges[i].m_cp;
        m_edgeTarget[slot] = edges[i].m_child;
    }

    //按广度优先顺序求失败指针与输出：父节点总在子节点之前处理，失败指针指向的节点深度
    //更小，也已处理过
    m_output.assign(nodeCount, -1);
    std::vector<int> bfs;
    bfs.reserve(nodeCount);
    bfs.push_back(0);
    for (size_t k = 0; k < bfs.size(); ++k)
    {
        const int u = bfs[k];
        //本节点的输出：自身是词尾时取自身（以此结尾的最长词），否则沿用失败指针所指节点的输出
        if (u != 0)
            m_output[u] = (m_nodeWord[u] >= 0) ? m_nodeWord[u] : m_output[m_fail[u]];

        const uint32_t first = m_firstEdge[u];
        const uint32_t last = first + m_edgeCount[u];
        for (uint32_t e = first; e < last; ++e)
        {
            const uint32_t cp = m_edgeCp[e];
            const int v = m_edgeTarget[e];
            int fail = 0;
            if (u != 0)
            {
                int f = m_fail[u];
                while (f != 0 && FindChild(f, cp) < 0)
                    f = m_fail[f];
                const int next = FindChild(f, cp);
                if (next >= 0)
                    fail = next;
            }
            m_fail[v] = fail;
            bfs.push_back(v);
        }
    }
    return m_words.size();
}

bool SensitiveWordMatcher::Match(const std::string& utf8, SensitiveHit* hit) const
{
    if (m_words.empty() || utf8.empty())
        return false;

    std::vector<uint32_t> cps;
    std::vector<uint32_t> origIndex;
    Normalize(utf8, cps, &origIndex);

    int state = 0;
    for (size_t i = 0; i < cps.size(); ++i)
    {
        const uint32_t cp = cps[i];
        while (state != 0 && FindChild(state, cp) < 0)
            state = m_fail[state];
        const int next = FindChild(state, cp);
        state = (next >= 0) ? next : 0;

        //第一次有输出即是最先结束的命中；输出本身已是在此结束的最长词
        const int output = m_output[state];
        if (output < 0)
            continue;

        if (hit != NULL)
        {
            const WordInfo& w = m_words[output];
            const size_t startNorm = i + 1 - w.m_length;
            hit->m_wordId = w.m_id;
            hit->m_category = w.m_category;
            hit->m_beginChar = origIndex[startNorm];
            hit->m_endChar = origIndex[i] + 1;
        }
        return true;
    }
    return false;
}

std::string SensitiveWordMatcher::MakeSnippet(const std::string& utf8, uint32_t beginChar,
                                              uint32_t endChar, uint32_t contextChars)
{
    std::vector<uint32_t> chars;
    DecodeUtf8(utf8, chars);
    const uint32_t n = static_cast<uint32_t>(chars.size());

    //区间越界或颠倒时夹回合法范围
    if (beginChar > n)
        beginChar = n;
    if (endChar > n)
        endChar = n;
    if (endChar < beginChar)
        endChar = beginChar;

    const uint32_t start = (beginChar > contextChars) ? (beginChar - contextChars) : 0;
    const uint32_t stop = (n - endChar > contextChars) ? (endChar + contextChars) : n;

    std::string out;
    if (start > 0)
        AppendUtf8(kEllipsis, out);
    for (uint32_t i = start; i < stop; ++i)
        AppendUtf8(chars[i], out);
    if (stop < n)
        AppendUtf8(kEllipsis, out);
    return out;
}

size_t SensitiveWordMatcher::WordCount() const
{
    return m_words.size();
}

int SensitiveWordMatcher::FindChild(int node, uint32_t cp) const
{
    //在本节点的出边区间 [lo, hi) 里按码点二分。直接用下标而不用标准库迭代器，
    //Debug 构建下没有迭代器检查的开销
    uint32_t lo = m_firstEdge[node];
    uint32_t hi = lo + m_edgeCount[node];
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (m_edgeCp[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < m_firstEdge[node] + m_edgeCount[node] && m_edgeCp[lo] == cp)
        return m_edgeTarget[lo];
    return -1;
}

} // namespace chatserver
