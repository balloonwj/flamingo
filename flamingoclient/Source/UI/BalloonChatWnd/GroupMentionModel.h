/**
 *  GroupMentionModel.h
 *
 *  群聊「@提醒」的<u>纯逻辑</u>部分 —— 偏移口径换算、发送前的文本重扫、
 *  「是否@到我」的判定。不依赖 HWND / balloonui / ATL / 业务对象，只用
 *  std::string 与 std::vector，便于被 FlamingoUnit 测试工程直接调用断言。
 *
 *  为什么会有两套偏移口径（这是本模块存在的主要原因）：
 *    · 协议侧（MentionSpan）用 <u>UTF-8 字节</u>偏移 —— 消息正文在协议里
 *      就是 UTF-8，按字节记跨端最稳，不依赖任何一端的字符串内部表示。
 *    · 渲染侧（RenderSpan）用 <u>UTF-16 字符</u>下标 —— 气泡高亮要调
 *      GDI+ 的 MeasureCharacterRanges，它按 WCHAR 计数。
 *  换算只在「协议结构 → 展示结构」这一处做，就是本文件的
 *  ConvertSpansForRender。
 *
 *  整体方案见 docs/群聊@提醒方案.md。
 *
 *  本文件不 include stdafx.h，工程里须把对应 .cpp 标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <string>
#include <vector>

namespace balloonchatwnd {

// 「@所有人」在协议与本模块里统一用 uid 0 表示 —— 真实 userid 恒 > 0，
// 不会与之冲突。
const long long kMentionUidAll = 0;

// 「@所有人」在输入框与消息正文里的显示名（不含 '@'）。
// 刻意写成显式 UTF-8 转义而非直接写汉字：本文件要被两个工程共用，源文件编码一旦
// 被工具改写，直接量里的中文就会变成别的字节，而这个串是要参与<u>文本匹配</u>的
// （RescanMentions 按它在正文里找 "@所有人"），一旦变了会静默匹配失败、极难排查。
const char* const kMentionAllNameUtf8 = "\xE6\x89\x80\xE6\x9C\x89\xE4\xBA\xBA";   // "所有人"

// 群成员角色取值，与服务端 group_member_role 对齐（见 net/GroupProtocolClient.h
// 的 GroupMemberItem::m_role）。本模块只用它判"能不能 @所有人"。
const int kGroupRoleMemberVal = 0;   // 普通成员
const int kGroupRoleAdminVal  = 1;   // 管理员
const int kGroupRoleOwnerVal  = 2;   // 群主

/**
 *  判定某个角色能否使用「@所有人」。
 *
 *  只有群主与管理员可以 —— @所有人 会给全群每个人都推一次提醒，放开给普通成员
 *  等于把打扰全群的权力交给任何人。与服务端的校验口径须一致（服务端校验待第二期
 *  接入，本函数是客户端侧的先行拦截，<u>不能</u>作为唯一防线）。
 *    role：群成员角色，取值见上面三个常量；越界值一律按普通成员处理。
 *  @return 允许返回 true。
 */
bool CanMentionAll(int role);

/**
 *  协议侧的一个 @ 片段，对应 content.at.users 数组的一项。
 *  m_off / m_len 是该「@显示名」片段在消息正文（UTF-8）里的<u>字节</u>偏移与长度。
 *  约定：'@' 符号计入片段，其后的分隔空格<u>不</u>计入。
 */
struct MentionSpan
{
    long long m_uid;   // 被 @ 的 userid；kMentionUidAll(0) 表示 @所有人
    int       m_off;   // 片段起始位置（UTF-8 字节偏移，>= 0）
    int       m_len;   // 片段长度（UTF-8 字节数，> 0）

    MentionSpan()
        : m_uid(0)
        , m_off(0)
        , m_len(0)
    {
    }
};

/**
 *  渲染侧的一个 @ 片段，供气泡按字符下标向 GDI+ 要高亮矩形。
 *  m_off / m_len 是 UTF-16 字符（WCHAR）口径；m_isMe 决定用橙色还是蓝色。
 */
struct RenderSpan
{
    long long m_uid;    // 被 @ 的 userid；kMentionUidAll(0) 表示 @所有人
    int       m_off;    // 片段起始位置（UTF-16 字符下标）
    int       m_len;    // 片段长度（UTF-16 字符数）
    bool      m_isMe;   // 被 @ 的是不是本人 —— 决定配色

    RenderSpan()
        : m_uid(0)
        , m_off(0)
        , m_len(0)
        , m_isMe(false)
    {
    }
};

/**
 *  发送前重扫用的一条候选记录 —— 用户在输入框里插入过的一个 @。
 *  重扫时按 m_displayName 在最终文本里找，找得到才产出片段。
 */
struct MentionCandidate
{
    long long   m_uid;           // 被 @ 的 userid；kMentionUidAll(0) 表示 @所有人
    std::string m_displayName;   // 插入时用的显示名（UTF-8，不含 '@'）

    MentionCandidate()
        : m_uid(0)
    {
    }
};

/**
 *  把 UTF-8 字节偏移换算成 UTF-16 字符下标。
 *    utf8Text：消息正文（UTF-8）。
 *    byteOff：字节偏移，取值 [0, utf8Text.size()]；等于长度表示"末尾之后"，合法。
 *  @return 对应的 UTF-16 字符下标；byteOff 越界、落在多字节序列中间、或正文
 *          含非法 UTF-8 序列时返回 -1。调用方须判负。
 */
int Utf8ByteOffsetToUtf16Index(const std::string& utf8Text, int byteOff);

/**
 *  把协议侧片段整批换算成渲染侧片段，并按 myUid 标好 m_isMe。
 *  越界、长度非正、换算失败的片段会被<u>丢弃</u>（不抛异常、不中断整批），
 *  保证一条畸形数据不会让整条消息渲染不出来。
 *
 *  m_isMe 的判定<u>逐片段独立</u>：该片段 @ 的是本人、或者它就是「@所有人」那一段
 *  （uid == kMentionUidAll），才算"@到我"。<u>不能</u>因为整条消息带了 @所有人 就把
 *  里面 @ 别人的片段也染成"@我"色 —— "@所有人 @张三 辛苦了"这句里，`@张三` 那段
 *  说的是张三，不是我。
 *    utf8Text：消息正文（UTF-8）。
 *    spans：   协议侧片段（字节口径）。
 *    myUid：   本人 userid；传 0（未登录 / 未知）时 m_isMe 一律为假。
 *  @return 渲染侧片段，按输入顺序保留；输入为空则返回空。
 */
std::vector<RenderSpan> ConvertSpansForRender(const std::string& utf8Text,
                                              const std::vector<MentionSpan>& spans,
                                              long long myUid);

/**
 *  发送前重扫：以输入框的<u>最终文本</u>为准，重新定位每条候选记录的片段。
 *
 *  为什么必须重扫：用户插入 "@张三 " 之后还可能把它删掉一半、或在中间插字，
 *  插入时记下的偏移就不再准。重扫扫不到的候选直接作废，从根上避免发出去的
 *  偏移与正文对不上。
 *
 *  匹配规则：
 *    · 只有位于文本开头、或紧跟在空白字符之后的 '@' 才可能是 mention ——
 *      这样 "abc@example.com" 这类邮箱不会被误判。
 *    · 同一位置有多个候选能匹配时取<u>显示名最长</u>的（避免 "张三" 抢走
 *      本该属于 "张三丰" 的位置）。
 *    · 每条候选只会被消费一次，按候选表顺序分配 —— 群里有同名成员时，
 *      文本里第 N 次出现该名字就归第 N 条同名候选。
 *
 *    utf8Text：  输入框最终文本（UTF-8）。
 *    candidates：用户插入过的候选记录（按插入顺序）。
 *  @return 重扫命中的片段，按在文本中出现的先后排序；一个都没命中则返回空。
 */
std::vector<MentionSpan> RescanMentions(const std::string& utf8Text,
                                        const std::vector<MentionCandidate>& candidates);

/**
 *  判定本条消息是否 @ 到了本人。<u>只看 uid，不看文本</u> —— 群名片改了、
 *  群里有同名成员，判定都不受影响。
 *
 *  三种情况算命中：消息级的 atAll 为真；片段里有本人 uid；片段里有「@所有人」
 *  （uid == kMentionUidAll）。最后一条是防御性的 —— 正常发送方会同时置 atAll，
 *  但只要正文里确实有一段 @所有人，就该按 @ 到我处理，不依赖对端有没有把标志位写对。
 *    spans：协议侧片段。
 *    atAll：本条是否 @所有人。
 *    myUid：本人 userid；传 0（未登录 / 未知）时一律返回 false。
 *  @return @ 到本人返回 true，否则 false。
 */
bool IsMentioningMe(const std::vector<MentionSpan>& spans, bool atAll, long long myUid);

/**
 *  把一条消息的协议侧 @ 数据整体换算成气泡要用的渲染侧片段。
 *
 *  存在的意义是<u>统一入口</u>：实时推送与历史渲染必须经同一条路产出 ats，否则
 *  两处各写各的迟早跑偏 —— 曾经就是历史渲染那条漏了没填，导致重开聊天窗后
 *  @ 的高亮全部消失（实时收到时是好的，一重开就没了）。新增取 @ 数据的场景
 *  一律调本函数，不要再各自拼装。
 *
 *  与 ConvertSpansForRender 的区别只在于多接一个 atAll：本函数负责「这条消息
 *  到底带没带 @」这层判断，调用方不必自己写 `if (!ats.empty() || atAll)`。
 *    utf8Text：消息正文（UTF-8），偏移换算的基准。
 *    spans：   协议侧片段（字节口径），可为空。
 *    atAll：   本条是否 @所有人。
 *    myUid：   本人 userid；传 0（未登录 / 未知）时各片段的 m_isMe 一律为假。
 *  @return 渲染侧片段；本条不带 @ 时返回空 vector。
 */
std::vector<RenderSpan> BuildRenderSpansForMessage(const std::string& utf8Text,
                                                   const std::vector<MentionSpan>& spans,
                                                   bool atAll,
                                                   long long myUid);

/**
 *  这批片段里是否含「@所有人」。
 *
 *  发送时用来推导协议的 content.at.all 标志：正文里既然有一段 @所有人，标志位就
 *  必须置上，两者不能对不上 —— 对端的「是否@到我」判定要同时看标志位与片段
 *  （见 IsMentioningMe），标志漏置会让部分实现漏掉提醒。
 *
 *  之所以从片段<u>推导</u>而不是让调用方各自传：发送路径上「用户到底 @ 了谁」的
 *  唯一真相是发送前按最终文本重扫出来的那批片段（RescanMentions），标志位再单独
 *  记一份就会有两个真相，迟早不一致。
 *    spans：协议侧片段。
 *  @return 含 uid == kMentionUidAll 的片段则返回 true。
 */
bool HasMentionAll(const std::vector<MentionSpan>& spans);

} // namespace balloonchatwnd
