/**
 *  ChatMemoCard.h
 *
 *  群备忘卡片（msgtype=23，新建或修改群备忘时服务端以操作人的名义写入群聊，只有这条备忘的可见者看得到）在群聊窗里显示为一张
 *  卡片。卡片复用屏幕共享卡片的版式（kind 同为 kMsgScreenShare，screenShareIsMemo 为 true）：标题前画日历；标题行写备忘标题，
 *  副标题「张三新建了群备忘」「张三修改了群备忘」，描述块第一行「开始时间 · 重复规则」、第二行备注，状态行「提醒 3 人」或
 *  「提醒全体成员」，底部「查看」按钮。设计见 docs/备忘与提醒方案.md §7.3。
 *
 *  卡片内容是保存那一刻的快照，之后不改写。备忘被删除、或本人被移出提醒对象之后，本机的备忘簿里就没有这一条了，卡片转为
 *  失效状态：状态行写「备忘已删除，或你已不在提醒对象中」，不能再点。是否失效由宿主按备忘簿判断后经 SetMemoCardAvailable 设置。
 *
 *  本文件把 content 原文解析成卡片要显示的内容并套用到一条气泡数据上。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试。
 *
 *  典型用法（群聊窗收到或翻到一张群备忘卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(fromMe, _T("[群备忘]"));
 *      balloonchatwnd::ApplyMemoCard(msg, notify.m_memoCardContent, editorName, memo::NowLocalMinute());
 *      balloonchatwnd::SetMemoCardAvailable(msg, balloonchatwnd::IsMemoCardAvailable(bookLoaded, inBook));
 *      list->AppendMessage(msg);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include "ChatMockData.h"   // ChatMessage / kMsgScreenShare

namespace balloonchatwnd {

/**
 *  一张群备忘卡片 content 解析出来的内容（文字已从 UTF-8 转成 UTF-16）。
 */
struct MemoCardInfo
{
    long long m_memoId;        // 备忘 id；解析失败时为 0
    long long m_groupId;       // 所属群 id
    int       m_change;        // 这张卡片对应的操作，取值见 net/Protocol.h 的 memo_card_change
    CString   m_title;         // 备忘标题
    CString   m_note;          // 备注，可含换行；没有时为空
    int64_t   m_start;         // 开始时间，十进制 yyyymmddhhmm；content 里的时间格式不对时为 0
    int       m_advance;       // 提前分钟数
    int       m_repeat;        // 重复方式，取值见 memo_repeat_type
    int       m_weekdays;      // 每周重复的星期掩码
    int       m_count;         // 提醒次数上限，0 为永不结束
    bool      m_targetAll;     // 是否提醒全体成员
    int       m_targetCount;   // 不是提醒全体成员时，被提醒的人数
    long long m_editorId;      // 新建或修改的人的 userid（即卡片的发送者）
    CString   m_editorName;    // 新建或修改的人的昵称（服务端填写；取不到时为空）

    MemoCardInfo()
        : m_memoId(0), m_groupId(0), m_change(0), m_start(0), m_advance(0), m_repeat(0), m_weekdays(0), m_count(1)
        , m_targetAll(false), m_targetCount(0), m_editorId(0)
    {
    }
};

/**
 *  解析一张群备忘卡片的 content。
 *  @param contentJson content 原文（UTF-8 JSON）
 *  @param out         [出参] 解析结果；返回 false 时内容不确定
 *  @return content 是 JSON 对象且带正的 memoid 时返回 true
 */
bool ParseMemoCard(const std::string& contentJson, MemoCardInfo& out);

/**
 *  卡片副标题（已按界面语言翻译）：「我新建了群备忘」「我修改了群备忘」「张三新建了群备忘」「张三修改了群备忘」，
 *  名字取不到时为「新建了一条群备忘」「修改了一条群备忘」。
 *  @param editorName 操作人显示名；空串表示取不到
 *  @param fromMe     卡片是不是本人发的（发送者即操作人）
 *  @param change     取值见 memo_card_change；认不出的取值按「修改」处理
 */
CString MemoCardSubtitle(const CString& editorName, bool fromMe, int change);

/**
 *  描述块第一行：「开始时间 · 重复规则」，如「10月5日 09:30 · 每周一」。开始时间与 now 同一天时写「今天 09:30」，
 *  写法与群「备忘」页签的卡片相同（MemoViewModel 的 EventTimeText / RepeatText）。开始时间不可用时只写重复规则。
 *  @param info 卡片内容
 *  @param now  当前时间（本地分钟序号，memo::NowLocalMinute 的返回值）
 */
CString MemoCardTimeLine(const MemoCardInfo& info, int64_t now);

/**
 *  状态行文字：「提醒全体成员」或「提醒 3 人」，与群「备忘」页签卡片的写法相同（MemoViewModel 的 TargetsText）。
 */
CString MemoCardTargetsText(const MemoCardInfo& info);

/**
 *  把一张群备忘卡片套用到气泡数据上：kind 改为 kMsgScreenShare 并置 screenShareIsMemo，填写标题、副标题、描述、
 *  状态与按钮，content 原文存进 screenShareContent，text 填摘要「[群备忘] 标题」。套用后卡片为可用状态；
 *  content 解析不了时标题写「群备忘」，不可点。
 *  @param msg        [入出参] 气泡数据，fromMe 须已填好
 *  @param contentJson content 原文
 *  @param editorName 操作人显示名（宿主按群名片、通讯录取得）；空串时退回 content 里服务端写的昵称
 *  @param now        当前时间（本地分钟序号），决定开始时间写「今天」还是日期
 */
void ApplyMemoCard(ChatMessage& msg, const std::string& contentJson, const CString& editorName, int64_t now);

/**
 *  卡片此刻是否可用：本机的备忘簿还没加载完时一律按可用显示（不知道的时候不说它失效）；加载完之后，簿里有这一条才可用。
 *  @param bookLoaded 备忘簿是否已加载完（登录后的 2401 应答已到）
 *  @param inBook     备忘簿里是否有这张卡片的 memoid
 */
bool IsMemoCardAvailable(bool bookLoaded, bool inBook);

/**
 *  切换卡片的可用状态：可用时状态行写提醒对象、可以点开；失效时状态行写「备忘已删除，或你已不在提醒对象中」，不可点。
 *  内容不可用（infoCardRefId 为 0）的卡片不受影响。
 *  @param msg       [入出参] 群备忘卡片（不是群备忘卡片时不做任何事）
 *  @param available 是否可用，一般取 IsMemoCardAvailable 的结果
 *  @return 状态有变化返回 true，调用方据此决定是否刷新这一行
 */
bool SetMemoCardAvailable(ChatMessage& msg, bool available);

/**
 *  卡片当前是否可以点开（内容可用且未失效）。
 */
bool IsMemoCardOpenable(const ChatMessage& msg);

} // namespace balloonchatwnd
