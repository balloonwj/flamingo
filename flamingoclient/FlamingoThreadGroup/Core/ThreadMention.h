/**
 *  帖子群正文里的 @ 片段（纯逻辑，不含界面，docs/帖子群方案.md 14.4）：
 *    - 发送前按最终的线上正文重扫出 @ 片段（复用普通群的 balloonchatwnd::RescanMentions，口径完全相同）；
 *    - 显示时把片段从线上正文的 UTF-8 字节位置换算成只读富文本里的字符区间，界面据此加背景色。
 *
 *  片段的位置一律以「线上正文」为准：表情是 /f["编号"]、字面的 '/' 在表情标签前写成「//」（ThreadText.h）。富文本里
 *  一个表情只占一个字符（找不到表情图时显示成一段替代文字），「//」显示成一个 '/'，所以线上位置与显示位置不是一一对应的，
 *  要按 ParseWire 的同一套规则逐字换算。
 *
 *  典型用法：
 *      std::vector<ThreadAtSpan> at = RescanAtSpans(wire, candidates);                 //发帖、留言前
 *      std::vector<ThreadAtDisplay> ranges = AtSpansToDisplay(wire, post.m_at, myId, knownFaces, unknownChars);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <vector>

namespace threadgroup {

struct ThreadMember;

//「@所有人」的 userid：片段、候选与选人列表里都用 0 表示（与服务端 at 片段、普通群的 kMentionUidAll 相同，
//docs/帖子群方案.md §15）
const int64_t kThreadAtAllUserId = 0;

//正文里的一段「@名字」：位置是线上正文（UTF-8）里的字节偏移与长度，'@' 计入、其后的空格不计入（与服务端 at 字段相同）
struct ThreadAtSpan
{
    int64_t m_userId;   //被 @ 的人
    int     m_off;      //字节偏移
    int     m_len;      //字节长度

    ThreadAtSpan()
        : m_userId(0)
        , m_off(0)
        , m_len(0)
    {
    }
};

//发送前重扫用的一个候选：用户经选人浮层插入过的一个 @
struct ThreadAtCandidate
{
    int64_t      m_userId;   //被 @ 的人
    std::wstring m_name;     //插入时用的名字（不含 '@'）

    ThreadAtCandidate()
        : m_userId(0)
    {
    }
};

//一段 @ 在只读富文本里的字符区间 [m_start, m_end)
struct ThreadAtDisplay
{
    int     m_start;   //起始字符位置（相对于这段正文在富文本里的起点）
    int     m_end;     //结束字符位置（不含）
    bool    m_isMe;    //被 @ 的是不是本人（决定配色）；「@所有人」也按本人的配色
    int64_t m_userId;  //被 @ 的人；「@所有人」为 kThreadAtAllUserId（点它不弹菜单，docs/帖子群方案.md §15.1 第 4 条）

    ThreadAtDisplay()
        : m_start(0)
        , m_end(0)
        , m_isMe(false)
        , m_userId(0)
    {
    }
};

/**
 *  按最终的线上正文重扫 @ 片段：只有位于开头或空白之后的「@名字」算数，同一位置取名字最长的候选，
 *  每个候选只用一次（与普通群 balloonchatwnd::RescanMentions 相同）。
 *  @param wire       线上正文（表情已编码为 /f["编号"]）
 *  @param candidates 用户插入过的候选，按插入顺序
 *  @return 片段，按位置先后；没有命中时为空
 */
std::vector<ThreadAtSpan> RescanAtSpans(const std::wstring& wire, const std::vector<ThreadAtCandidate>& candidates);

/**
 *  从已有正文的 @ 片段取出候选（编辑帖子时用）：每段「@名字」里 '@' 之后的文字就是当时插入的名字。对方后来改了群名片，
 *  按现在的名字在原文里找不到，所以编辑时要用原文里的名字重扫，原有的 @ 才能保留下来。
 *  @param wire 原正文（线上形式）
 *  @param at   原正文的 @ 片段
 *  @return 候选，按片段先后；越界或不以 '@' 开头的片段跳过
 */
std::vector<ThreadAtCandidate> CandidatesFromSpans(const std::wstring& wire, const std::vector<ThreadAtSpan>& at);

/**
 *  把线上正文里的一段 [wireStart, wireEnd)（UTF-16 下标）换算成显示文字里的区间。
 *  表情标签在 knownFaceIds 里时算 1 个字符，否则算 unknownFaceChars 个字符（显示为替代文字）；「//」转义算 1 个字符。
 *  @return 两端都换算成功（区间不越界、不落在表情标签中间）时返回 true
 */
bool WireRangeToDisplay(const std::wstring& wire, int wireStart, int wireEnd, const std::set<int>& knownFaceIds,
                        int unknownFaceChars, int& dispStart, int& dispEnd);

/**
 *  把一段正文的全部 @ 片段换算成显示区间：越界、落在多字节字符中间、或对不上的片段丢弃，不影响其余片段。
 *  @param wire       线上正文
 *  @param at         片段（服务端下发的 UTF-8 字节位置）
 *  @param myUserId   本人；为 0 时 m_isMe 一律为 false
 */
std::vector<ThreadAtDisplay> AtSpansToDisplay(const std::wstring& wire, const std::vector<ThreadAtSpan>& at,
                                              int64_t myUserId, const std::set<int>& knownFaceIds, int unknownFaceChars);

/**
 *  插入正文的「所有人」三个字（不含 '@'）：这段文字会随正文发给全群，与普通群的 kMentionAllNameUtf8 相同，不随界面语言变化。
 */
std::wstring AtAllName();

//片段里有没有「@所有人」（userid 为 kThreadAtAllUserId 的一段）：发请求时据此带 mentionall
bool HasAtAll(const std::vector<ThreadAtSpan>& at);

/**
 *  在光标处插入「@名字」之前要不要先补一个空格（发帖窗口的「@ 成员」，2026-10-08 第 6h 步联调发现）：发送前的重扫只认
 *  位于正文开头、或紧跟 ASCII 空白（空格、制表符、回车、换行）之后的「@名字」（与普通群 RescanMentions 的口径相同），
 *  直接接在文字或表情后面插入的会被当成普通文字，@ 不生效。普通群聊窗 InsertMentionAtCaret 的做法相同。
 *  @param caretPos 插入位置（字符位置）；为 0 表示在正文开头
 *  @param prevChar 插入位置前面那一个字（caretPos 为 0 时不看）；表情图读出来是 U+FFFC
 *  @return 要补空格返回 true
 */
bool NeedsSpaceBeforeAt(int caretPos, wchar_t prevChar);

/**
 *  按字符位置查点中的是哪个人的 @（点 @XXX 弹成员菜单，docs/帖子群方案.md §15.1 第 4 条）。
 *  @param ranges    显示区间（AtSpansToDisplay 的结果，m_start / m_end 已是富文本里的绝对字符位置）
 *  @param charIndex 点中的字符位置
 *  @return 被 @ 的人；落在各段之外、或落在「@所有人」上时返回 0
 */
int64_t AtUserAtChar(const std::vector<ThreadAtDisplay>& ranges, int charIndex);

/**
 *  @ 成员列表的条目（docs/帖子群方案.md §15.1 第 1 条）：canMentionAll 为真时最上面是「所有人」（userid 为
 *  kThreadAtAllUserId，名字为 AtAllName()），其后是 order 里的成员，顺序不变。
 *  @param order         成员，已按 CThreadMemberBook::PickOrder 排好、去掉了本人
 *  @param canMentionAll 本人能否 @所有人（CanMentionAll）
 */
std::vector<ThreadMember> MentionPickList(const std::vector<ThreadMember>& order, bool canMentionAll);

} // namespace threadgroup
