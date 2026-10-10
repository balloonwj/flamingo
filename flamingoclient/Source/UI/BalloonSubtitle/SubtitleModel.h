/**
 *  SubtitleModel.h
 *
 *  聊天字幕模式的内容模型：字幕上当前显示哪一条、显示成什么样。规则（docs/聊天字幕模式方案.md 第 2 节）：
 *    · 从进入字幕模式的那一刻起，来一条新消息显示一条，再来一条就替换成新的；进入时不显示旧消息，
 *      第一条到达之前显示「等待新消息…」；
 *    · 本人发出的消息（含别的设备同步回来的）与系统提示不上字幕；
 *    · 同一条消息（同一个服务器消息 id）只显示一次，重复推送不会把字幕换回旧消息；
 *    · 正在显示的那条被撤回时，换成撤回提示（如「张三撤回了一条消息」）。
 *  文字内容由 BuildSubtitleContent 按消息类型生成：文本取正文，图片、语音、表情等取占位文字，
 *  文件补上文件名、语音补上时长；换行与连续空白合并成一个空格。
 *
 *  本模块不碰窗口，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用（见 Tests/SubtitleTests.cpp 的 SUBM 组）。
 *
 *  典型用法：
 *      subtitle::CSubtitleModel model;
 *      model.Reset();                                                    //进入字幕模式
 *      if (model.OnIncoming(subtitle::MakeSubtitleIncoming(notify, selfId, speaker)))
 *          view->SetContent(model.Current());                            //内容变了才重画
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <stdint.h>
#include <deque>
#include <map>
#include <string>

namespace net {
struct MessageNotify;
}

namespace subtitle {

//字幕上这一行内容的种类
enum SubtitleLineKind
{
    //进入字幕模式后还没有收到消息：显示「等待新消息…」，文字颜色较暗
    kLineWaiting = 0,
    //一条消息：显示「张三说：内容」，名字与内容分别着色
    kLineMessage = 1,
    //提示：正在显示的消息被撤回后显示撤回提示，只有内容、没有说话人
    kLineNotice  = 2,
};

//字幕上当前显示的内容
struct SubtitleLine
{
    int          m_kind;      //取值见 SubtitleLineKind
    std::wstring m_speaker;   //说话人显示名（不含「说：」）；kLineWaiting 与 kLineNotice 时为空
    std::wstring m_content;   //内容：消息正文或占位文字，已合并空白；kLineWaiting 时为提示语
    std::wstring m_origin;    //来源标注：群聊字幕为群名，画在第一行行首的「【群名】」里；单聊为空。三种种类都带
    long long    m_msgid;     //对应的服务器消息 id；kLineWaiting 时为 0

    SubtitleLine()
        : m_kind(kLineWaiting)
        , m_msgid(0)
    {
    }
};

//交给字幕的一条消息（由聊天窗从推送换算而来，见 MakeSubtitleIncoming）
struct SubtitleIncoming
{
    long long    m_msgid;     //服务器消息 id；不大于 0 表示未知，这种消息不参与去重
    bool         m_fromSelf;  //是否为本人发出（含别的设备同步回来的）；本人的消息不上字幕
    bool         m_isSystem;  //是否为系统提示（入群、改群名等由服务端生成的提示）；系统提示不上字幕
    std::wstring m_speaker;   //发送人显示名
    std::wstring m_content;   //内容：文本正文或占位文字（由 BuildSubtitleContent 生成）

    SubtitleIncoming()
        : m_msgid(0)
        , m_fromSelf(false)
        , m_isSystem(false)
    {
    }
};

//去重时记住的最近消息 id 个数：重复推送一般紧跟在原推送之后，几十条足够
const size_t kRecentMsgidCount = 64;
//内容的最大字符数（UTF-16 单元）：字幕最多三行，超出这个长度的部分无论如何显示不下，先截掉再排版，
//排版时只需测量有限长度的文字
const size_t kMaxContentChars = 1000;

/**
 *  CSubtitleModel —— 一条字幕的内容状态。
 *
 *  由 CSubtitleSession 持有，只在界面线程上使用。
 */
class CSubtitleModel
{
public:
    //构造后处于「等待新消息」状态
    CSubtitleModel();

    /**
     *  回到「等待新消息」状态，并清空去重记录。进入字幕模式时调用。
     */
    void Reset();

    /**
     *  收到一条消息。
     *    in：换算好的消息。
     *  返回：字幕内容因此变化返回 true（调用方据此重画）；本人的消息、系统提示、已经显示过的
     *        同一条消息返回 false，内容保持不变。
     */
    bool OnIncoming(const SubtitleIncoming& in);

    /**
     *  有消息被撤回。
     *    msgid：     被撤回消息的服务器消息 id。
     *    noticeText：撤回提示，如「张三撤回了一条消息」（调用方按撤回者拼好）。
     *  返回：正在显示的正是这条、字幕换成了撤回提示时返回 true；否则返回 false，内容不变。
     */
    bool OnRecalled(long long msgid, const std::wstring& noticeText);

    /**
     *  设置来源标注（群聊字幕为群名，用户 2026-10-05 确认群聊字幕要标出来自哪个群）。当前内容与之后的每一条
     *  都带上它；Reset 不清除。
     *    origin：群名；单聊传空串。
     */
    void SetOrigin(const std::wstring& origin);

    //当前显示的内容
    const SubtitleLine& Current() const;

private:
    /**
     *  记下一条消息 id，用于去重。
     *    msgid：服务器消息 id；不大于 0 时不记录，直接返回 true。
     *  返回：这条以前没出现过返回 true；已经出现过返回 false。
     */
    bool rememberMsgid(long long msgid);

    SubtitleLine          m_current;  //当前显示的内容
    std::deque<long long> m_recent;   //最近出现过的消息 id，最旧的在前，最多 kRecentMsgidCount 个
    std::wstring          m_origin;   //来源标注（群名）；单聊为空，由 SetOrigin 设置
};

/**
 *  把换行、制表符与连续的空白合并成一个空格，并去掉首尾空白。
 *    s：原文。
 *  返回：整理后的文字。
 */
std::wstring CollapseWhitespace(const std::wstring& s);

/**
 *  按推送生成字幕上的内容文字。
 *    notify：消息推送。
 *  返回：文本消息为正文（内联表情换成「[表情]」）；其它类型为占位文字，与会话列表的预览同一口径
 *        （net::MessageProtocolClient::digestForNotify），其中文件补上文件名（「[文件] 报价单.xlsx」）、
 *        语音补上时长（「[语音] 8″」）。结果已合并空白，超过 kMaxContentChars 的部分截掉并以省略号结尾。
 */
std::wstring BuildSubtitleContent(const net::MessageNotify& notify);

/**
 *  把一条推送换算成交给字幕的消息。
 *    notify：    消息推送。
 *    selfUserId：本人 userid；为 0 时视为未知，不判定本人消息。
 *    speaker：   发送人显示名（单聊为对方的名字，群聊为群名片或昵称），由调用方按会话上下文解析。
 *  返回：换算结果。
 */
SubtitleIncoming MakeSubtitleIncoming(const net::MessageNotify& notify, int64_t selfUserId,
                                      const std::wstring& speaker);

/**
 *  「等待新消息…」提示语（已按界面语言翻译）。
 */
const wchar_t* WaitingHintText();

/**
 *  CGroupCardCache —— 主面板为开着字幕的群记下的群名片。
 *
 *  群聊字幕上的说话人群名片优先（用户 2026-10-05 确认），但字幕不随群聊窗关闭而关闭，而群名片原先只存在
 *  群聊窗里。主面板在群字幕开启时请求一次群成员列表，成员列表应答经过主面板时把开着字幕的群的群名片记在这里。
 *  只在界面线程上使用（见 Tests/SubtitleTests.cpp 的 SUBGC1）。
 *
 *  典型用法（主面板里）：
 *      m_groupCards.SetGroupCards(groupId, cards);                          //收到成员列表
 *      speaker = m_groupCards.ResolveSpeaker(groupId, fromUserId, ContactSpeakerName(fromUserId));
 *      m_groupCards.ForgetGroup(groupId);                                   //群字幕关闭
 */
class CGroupCardCache
{
public:
    /**
     *  记下一个群全部成员的群名片，替换这个群以前记下的。
     *    groupId：群ID。
     *    cards：  成员 userid → 群名片；没设群名片的成员可以不放，或放空串。
     */
    void SetGroupCards(int64_t groupId, const std::map<int64_t, std::wstring>& cards);

    /**
     *  忘掉一个群的群名片（群字幕关闭、退出群聊时）。
     *    groupId：群ID。
     */
    void ForgetGroup(int64_t groupId);

    //全部忘掉（注销时）
    void Clear();

    /**
     *  是否记着这个群的群名片（决定开启群字幕时要不要请求成员列表）。
     *    groupId：群ID。
     */
    bool HasGroup(int64_t groupId) const;

    /**
     *  群聊字幕上的说话人名字：这个群里该成员有群名片（去掉首尾空白后不为空）就用群名片，否则用 fallback。
     *    groupId： 群ID。
     *    userId：  发送人 userid。
     *    fallback：没有群名片时用的名字（主面板传联系人短名）。
     *  返回：说话人名字。
     */
    std::wstring ResolveSpeaker(int64_t groupId, int64_t userId, const std::wstring& fallback) const;

private:
    std::map<int64_t, std::map<int64_t, std::wstring> > m_cards;  //群ID → （成员 userid → 群名片）
};

} // namespace subtitle
