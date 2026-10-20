/**
 *  ChatMessageList.h
 *
 *  单聊窗的消息区 —— 一个可滚动的消息行容器。每个子控件是一条
 *  ChatBubble（变高行）；本控件按各行的 GetRowHeight() 自上而下堆叠，
 *  超出视口即可用鼠标滚轮滚动，右侧画一条细滚动条。
 *
 *  本控件还承载消息的"多选模式"：进入后底部让出一条操作条（取消 /
 *  逐条转发 N / 合并转发 N / 删除 N），各 ChatBubble 行首显示勾选圈。复制 / 删除 /
 *  撤回 / 转发 / 引用回复等单条操作由 ChatBubble 的右键菜单回调到
 *  本类对应的 RequestXxx 接口。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <memory>
#include <set>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatMockData.h"
#include "ChatSmoothScroll.h"   // 滚轮的平滑滚动（成员需完整类型）
#include "ChatScrollFade.h"     // 滑块的淡入淡出（成员需完整类型）

/**
 *  消息列表请宿主聊天窗删除消息（右键「删除」或多选「删除 N」里已有服务器 msgid 的那些行）。
 *  lParam = 堆上 std::vector<long long>*（要删除的服务器 msgid，均为正数、至少一个），
 *  宿主处理后 delete；异步消息。宿主弹确认框，确认后逐条发 cmd_delete_message；
 *  气泡等服务端推来 cmd_messages_deleted_notify 后才移除（docs/清理聊天记录方案.md §10）。
 *
 *  列表只投给自己所在的聊天窗，按开发约定文档「UI 约定」第 6 条属窗口私有消息，走 WM_APP 段，
 *  因此定义在这里而不是 CustomMsgDef.h（后者只放 WM_USER 段的窗口间消息，消息号守卫要求
 *  其中的号都低于代理窗闸门下界）。两个聊天窗的头文件为消息映射包含了本文件。
 */
#define WM_CHAT_DELETE_REQUEST  (WM_APP + 0x61)

/**
 *  消息列表请宿主聊天窗转发多选中的消息（多选操作条上的「逐条转发」「合并转发」）。
 *  wParam 不用（恒为 0）；lParam = 堆上 balloonchatwnd::ChatForwardRequest*，宿主处理后
 *  delete；异步消息。宿主补上源会话信息后交给主面板打开目标选择窗。
 *  与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息，理由见上。
 */
#define WM_CHAT_FORWARD_REQUEST (WM_APP + 0x62)

/**
 *  合并卡片请宿主聊天窗打开聊天记录查看窗（单击了卡片）。
 *  wParam 不用（恒为 0）；lParam = 堆上 std::string*（合并消息的 content 原文，UTF-8），
 *  宿主转投给主面板（FMG_MSG_OPEN_MERGED_VIEWER，所有权随之转交）或自行 delete；异步消息。
 *  与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息，理由见上。
 */
#define WM_CHAT_OPEN_MERGED_RECORD (WM_APP + 0x64)

/**
 *  接龙卡片或带 cardmsgid 的接龙系统提示行请宿主聊天窗打开接龙详情窗（单击了卡片、卡片按钮或提示行）。
 *  wParam = ChatRelayAction 取值，区分只打开详情窗还是打开后把焦点放到本人那条的输入框；
 *  lParam = 堆上 long long*（卡片消息的服务器 msgid），宿主处理后 delete；异步消息。
 *  msgid 放在堆上而不直接放进 lParam，是因为 Win32 下 LPARAM 只有 32 位，装不下 64 位 msgid。
 *  与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息，理由见上。
 */
#define WM_CHAT_RELAY_ACTION (WM_APP + 0x66)

/**
 *  气泡右键「重新编辑」请宿主聊天窗把原文放回输入框（docs/敏感词方案.md 8.4）。
 *  lParam = 堆上 balloonchatwnd::ChatReeditData*（片段与 @ 候选），宿主处理后 delete；wParam 未用；异步消息。
 *  与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息，理由见上。
 */
#define WM_CHAT_REEDIT_REQUEST (WM_APP + 0x68)

/**
 *  共享卡片请宿主聊天窗打开那个共享（单击了卡片或卡片上的「打开」「下载」「在本机打开」，docs/文件共享方案.md 7.3）。
 *  lParam = 堆上 long long*（共享 id），宿主处理后 delete；wParam 为 kChatOpenShareOwn 时是本人发起的共享，否则为 0；
 *  异步消息。宿主经文件共享宿主交给 FlamingoFileShare.dll：别人的共享打开浏览窗口，本人的共享在本机打开。
 *  id 放在堆上的理由同 WM_CHAT_RELAY_ACTION。
 *  方案原定的 0x66 已被接龙卡片占用，改取 0x69。与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息，理由见上。
 */
#define WM_CHAT_OPEN_SHARE (WM_APP + 0x69)

//WM_CHAT_OPEN_SHARE 的 wParam：卡片是本人发起的共享，宿主改为在本机打开（2026-10-03 起）
const WPARAM kChatOpenShareOwn = 1;

/**
 *  屏幕共享卡片请宿主聊天窗加入那场共享（单击了可加入的卡片或卡片上的「加入」，docs/屏幕共享与录制方案.md 5.2 第 4 步）。
 *  lParam = 堆上 balloonchatwnd::ScreenShareJoinRequest*（房间号与发起人名），宿主处理后 delete；wParam 未用；异步消息。
 *  宿主经屏幕共享中心交给 FlamingoMedia.dll 加入。与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息，理由见上。
 */
#define WM_CHAT_JOIN_SCREENSHARE (WM_APP + 0x6A)

//语音气泡请宿主聊天窗播放 / 停止，或重发发送失败的语音（语音消息第 3d 步）：wParam 为动作（ChatVoiceAction，见
//ChatVoiceBubble.h），lParam 为堆上的 VoiceActionRequest*，宿主处理后 delete
#define WM_CHAT_VOICE_ACTION (WM_APP + 0x82)

/**
 *  入群邀请卡片请宿主单聊窗加入那个群（被邀请人点了卡片上的「加入群聊」，docs/邀请入群方案.md §3.3、§6.4）。
 *  lParam = 堆上 balloonchatwnd::GroupInviteJoinRequest*（卡片 msgid 与邀请 id），宿主处理后 delete；wParam 未用；
 *  异步消息。宿主把卡片置为「正在加入」并发 cmd_accept_group_invite（1327）。与 WM_CHAT_DELETE_REQUEST 同属
 *  窗口私有消息，理由见上。0x6B ~ 0x6E 已被主面板搜索下拉占用，取 0x6F。
 */
#define WM_CHAT_JOIN_GROUP_INVITE (WM_APP + 0x6F)

/**
 *  群公告卡片请宿主群聊窗打开这条公告的详情窗（点了可点的群公告卡片，docs/群公告接入方案.md §13.4）。
 *  lParam = 堆上 balloonchatwnd::InfoCardOpenRequest*（卡片 msgid 与公告 id），宿主处理后 delete；wParam 未用；异步消息。
 *  与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息。0x80 ~ 0x82 已占用，取 0x83。
 */
#define WM_CHAT_OPEN_ANNOUNCEMENT_CARD (WM_APP + 0x83)

/**
 *  群备忘卡片请宿主群聊窗打开这条备忘（点了可点的群备忘卡片，docs/备忘与提醒方案.md §7.3）：群主、管理员以修改方式打开，
 *  其他人只读。lParam = 堆上 balloonchatwnd::InfoCardOpenRequest*（卡片 msgid 与备忘 id），宿主处理后 delete；wParam 未用。
 */
#define WM_CHAT_OPEN_MEMO_CARD (WM_APP + 0x84)

/**
 *  代码片段卡片请宿主聊天窗打开代码查看窗（点了卡片正文、底栏「查看全部」或右键「在窗口中查看」，
 *  docs/代码片段方案.md §3.2）。lParam = 堆上 balloonchatwnd::CodeViewerOpenRequest*，宿主处理后 delete；
 *  wParam 未用；异步消息。与 WM_CHAT_DELETE_REQUEST 同属窗口私有消息。0x85 已被 Markdown 格式命令占用，取 0x86。
 */
#define WM_CHAT_OPEN_CODE_VIEWER (WM_APP + 0x86)

/**
 *  Markdown 卡片请宿主聊天窗打开 Markdown 预览窗口（点了卡片或右键「在窗口中查看」，docs/Markdown消息方案.md 第 11.3 节）。
 *  lParam = 堆上 balloonchatwnd::MarkdownPreviewOpenRequest*，宿主处理后 delete；wParam 未用；异步消息。
 *  0x86 ~ 0x89 已被代码片段占用，取 0x8A。
 */
#define WM_CHAT_OPEN_MARKDOWN_PREVIEW (WM_APP + 0x8A)

/**
 *  文字消息右键选了「添加到备忘」/「添加到我的备忘」/「添加到群备忘」，请宿主聊天窗补上来源信息（对方或发送人的
 *  名字、群名）后打开新建备忘的窗口并预填内容（docs/备忘与提醒方案.md §8）。
 *  lParam = 堆上 balloonchatwnd::ChatAddToMemoRequest*（ChatMemoPrefill.h），宿主处理后 delete；wParam 未用；异步消息。
 *  0x87 ~ 0x8C 已被代码片段、Markdown 编辑窗与 Markdown 预览窗占用，取 0x8D。
 */
#define WM_CHAT_ADD_TO_MEMO (WM_APP + 0x8D)

/**
 *  云端录像卡片请宿主聊天窗播放这条录像（点了可点的云端录像卡片，docs/屏幕共享与录制方案.md 12.6.5）。
 *  lParam = 堆上 balloonchatwnd::CloudRecordOpenRequest*（录像 id 与标题），宿主处理后 delete；wParam 未用；异步消息。
 *  0x8E 已被代码查看窗占用，取 0x8F。
 */
#define WM_CHAT_OPEN_CLOUD_RECORD_CARD (WM_APP + 0x8F)

/**
 *  密聊邀请卡片请宿主单聊窗执行卡片上的按钮（被邀请方点了「同意」「忽略」，发起方点了「取消」，docs/密聊方案.md 4.8）。
 *  wParam = 动作，取值见 ChatSecretMode.h 的 ChatSecretInviteAction；lParam = 堆上 unsigned long long*（卡片上的密聊会话号），
 *  宿主处理后 delete；异步消息。会话号放在堆上的理由同 WM_CHAT_RELAY_ACTION。方案初稿写的 0x8A 已被 Markdown 卡片占用，
 *  0xB0、0xB1 已被远程桌面占用，取 0xB2。
 */
#define WM_CHAT_SECRET_INVITE_ACTION (WM_APP + 0xB2)

namespace balloonchatwnd {

/**
 *  MarkdownPreviewOpenRequest —— WM_CHAT_OPEN_MARKDOWN_PREVIEW 的载荷：要预览的文档与它是谁、什么时候发的。
 */
struct MarkdownPreviewOpenRequest
{
    std::string  m_sourceUtf8;    //Markdown 源文本（UTF-8，正文转义已还原）
    std::wstring m_title;         //卡片标题，作为预览窗口的标题
    std::wstring m_sender;        //发送者显示名；单聊对方的消息可能为空，由宿主补上
    bool         m_fromMe;        //是否本人发出
    long long    m_timestampMs;   //发送时间（毫秒）；还没收到回执的本人消息为 0
    std::string  m_key;           //这条消息的标识（服务器 msgid 的十进制串；还没有 msgid 时为 clientMsgId），
                                  //预览窗口据此复用同一条消息已打开的窗口

    MarkdownPreviewOpenRequest()
        : m_fromMe(false)
        , m_timestampMs(0)
    {
    }
};

/**
 *  InfoCardOpenRequest —— WM_CHAT_OPEN_ANNOUNCEMENT_CARD / WM_CHAT_OPEN_MEMO_CARD 的载荷：点的是哪张卡片。
 */
struct InfoCardOpenRequest
{
    long long m_msgid;   //卡片消息的 msgid，宿主据此取出那一行的 content
    long long m_refId;   //卡片指向的公告 id 或备忘 id（ChatMessage::infoCardRefId）

    InfoCardOpenRequest()
        : m_msgid(0)
        , m_refId(0)
    {
    }
};

/**
 *  CloudRecordOpenRequest —— WM_CHAT_OPEN_CLOUD_RECORD_CARD 的载荷：要播放的是哪条云端录像。
 */
struct CloudRecordOpenRequest
{
    long long m_recordId;   //录像 id（ChatMessage::infoCardRefId），大于 0
    CString   m_title;      //录像标题（ChatMessage::screenShareTitle），作本机缓存文件名与提示用

    CloudRecordOpenRequest()
        : m_recordId(0)
    {
    }
};

/**
 *  CodeViewerOpenRequest::m_action 的取值：收到请求的宿主要做什么。
 */
enum CodeViewerAction
{
    kCodeViewerActionView     = 0,   //打开代码查看窗：点了代码卡片的正文、底栏，或右键「在窗口中查看」
    kCodeViewerActionAnnotate = 1,   //给这条代码片段追加批注（docs/代码片段方案.md 第 12 节）：聊天卡片右键「添加批注…」，宿主打开
                                     //可追加批注、展开了批注栏的代码查看窗。只有单聊窗、群聊窗处理，其余宿主忽略
};

/**
 *  CodeViewerOpenRequest —— WM_CHAT_OPEN_CODE_VIEWER 的载荷：要在查看窗里显示、或要追加批注的代码。
 */
struct CodeViewerOpenRequest
{
    int          m_action;   //要做的事，取值见 CodeViewerAction
    std::wstring m_code;     //规范化后的代码原文（换行为 LF）
    int          m_lang;     //语言，取值见 codesnippet::CodeLanguage
    std::string  m_key;      //这条消息的标识（服务器 msgid 的十进制串；还没有 msgid 时为 clientMsgId），
                             //查看窗据此复用同一条消息已打开的窗口
    std::vector<codesnippet::CodeNote> m_notes;   //批注，行号指 m_code 的行；没有批注时为空（docs/代码片段方案.md 第 10.5 节）
    long long    m_msgid;      //这条消息的服务器 msgid；还没有时为 0，此时不能追加批注
    long long    m_senderId;   //这条消息的发送者 userid；0 表示不知道（单聊里宿主按 m_fromMe 推出）
    bool         m_fromMe;     //这条消息是不是本人发的

    CodeViewerOpenRequest()
        : m_action(kCodeViewerActionView)
        , m_lang(0)
        , m_msgid(0)
        , m_senderId(0)
        , m_fromMe(false)
    {
    }
};

/**
 *  GroupInviteJoinRequest —— WM_CHAT_JOIN_GROUP_INVITE 的载荷：点的是哪张入群邀请卡片。
 */
struct GroupInviteJoinRequest
{
    long long m_msgid;      //卡片消息的 msgid，宿主据此找到那一行并置为「正在加入」
    long long m_inviteId;   //卡片上的邀请 id，随 1327 发给服务端

    GroupInviteJoinRequest()
        : m_msgid(0)
        , m_inviteId(0)
    {
    }
};

//WM_CHAT_RELAY_ACTION 的 wParam：用户点的是卡片的哪一部分
enum ChatRelayAction
{
    kChatRelayActionOpen = 0,   //点了卡片的空白处或提示行：只打开详情窗
    kChatRelayActionJoin = 1,   //点了卡片底部的「参与接龙」「修改我的」：打开详情窗并把焦点放到本人那条的输入框
};

/**
 *  WM_CHAT_FORWARD_REQUEST 的负载：用户在多选操作条上要转发哪些消息、用哪种模式。
 *  由 ChatMessageList::RequestForwardSelected 在堆上创建，宿主聊天窗处理后 delete。
 */
struct ChatForwardRequest
{
    bool                   m_merge;     //true 合并转发为一条聊天记录，false 逐条转发
    //要转发的服务器 msgid，按 msgid 升序、去重。逐条转发且选中的只有在线直传文件时可以为空
    std::vector<long long> m_msgids;
    size_t                 m_skipped;   //选中了但不能转发的行数，供宿主提示「已跳过 N 条」
    //逐条转发时要以离线文件重新发送的在线直传文件的本机路径（规则见 balloonmain::DecideDirectFileForward）
    std::vector<std::wstring> m_reuploadFiles;
    //逐条转发时本机已没有文件、无法转发的在线直传文件条数，不计入 m_skipped。右键「转发」一条这样的消息时，
    //列表投来的请求只有这一项为 1，宿主据此提示「在线直传的文件无法转发」
    size_t                 m_skippedDirect;
    //合并转发时进入记录的在线直传文件条数（文件本身不随记录发出，对方在记录里看到说明文字）
    size_t                 m_directPlaceholders;

    ChatForwardRequest()
        : m_merge(false)
        , m_skipped(0)
        , m_skippedDirect(0)
        , m_directPlaceholders(0)
    {
    }
};

/**
 *  多选操作条上的按钮，从左到右四等分排列。
 */
enum MultiSelectBarButton
{
    kBarButtonNone          = 0,   // 没有命中任何按钮（落点在操作条之外）
    kBarButtonCancel        = 1,   // 取消：退出多选
    kBarButtonForwardEach   = 2,   // 逐条转发：选中的每条消息在目标会话里各生成一条
    kBarButtonForwardMerged = 3,   // 合并转发：选中的消息打包成一条聊天记录
    kBarButtonDelete        = 4    // 删除：删除选中的消息
};

/**
 *  判断落点命中多选操作条上的哪个按钮。四个按钮等分操作条宽度，分界点归右侧那一段。
 *  绘制与点击共用本函数和 MultiSelectBarButtonRect，避免两处的分段随时间漂移。
 *    bar：操作条矩形（客户区坐标）。
 *    pt： 落点（与 bar 同一坐标系）。
 *  @return 命中的按钮；落点在操作条之外时返回 kBarButtonNone。本函数无副作用。
 */
MultiSelectBarButton MultiSelectBarHitTest(const RECT& bar, POINT pt);

/**
 *  取多选操作条上某个按钮的矩形。
 *    bar：    操作条矩形。
 *    button： 按钮，取 kBarButtonCancel ~ kBarButtonDelete；kBarButtonNone 返回空矩形。
 *  @return 按钮矩形（与 bar 同一坐标系）。最后一个按钮的右边界取 bar.right，吸收整除余数。
 */
RECT MultiSelectBarButtonRect(const RECT& bar, MultiSelectBarButton button);

class ChatMessageList;

/**
 *  平滑滚动逐帧驱动与消息列表之间的"主人令牌"。
 *
 *  逐帧驱动交给 balloonui 的 DuiAnimMgr（进程级 60Hz 脉冲）调度，由它持有；消息列表
 *  可能先于驱动被销毁（关窗时动画还没走完）。驱动与列表共用一份令牌：列表析构时把
 *  m_owner 置空，驱动下次触发时读到空就什么都不做。令牌本身由 shared_ptr 保活，读
 *  m_owner 永远安全。做法与 balloonui 的 DuiSwitch 相同。
 */
struct ChatScrollAnimToken
{
    ChatMessageList* m_owner;   // 所属消息列表；列表析构时置 NULL

    ChatScrollAnimToken()
        : m_owner(NULL)
    {
    }
};

/**
 *  把一段 Unicode 文字放进剪贴板。
 *
 *  放在本文件是因为"选中一段文字然后复制"有<u>两条</u>入口 —— 气泡的右键「复制」
 *  与消息区的 Ctrl+C —— 两处必须写出同样的剪贴板内容，实现只该有一份。之所以不
 *  放进 ChatTextSelection：那是明确的<u>纯逻辑</u>模块，不引 windows.h，而剪贴板
 *  少不了 HWND。
 *    owner：拥有本次剪贴板会话的窗口。
 *    text： 要复制的文字；NULL 或空串时什么都不做。
 */
void PutTextToClipboard(HWND owner, LPCWSTR text);

class ChatBubble;

/**
 *  ChatMessageList —— 可滚动的消息行容器。
 */
class ChatMessageList : public balloonwjui::DuiControl
{
public:
    ChatMessageList();
    ~ChatMessageList();

    // 把滚动位置移到底部（显示最新消息）。须在填充消息后调用一次。
    // 本控件已经摆好位置时当场重新排版，滚动位置与滚动条随即就是正确的；尚未摆好位置时
    // 由之后的第一次排版夹紧。副作用：取消进行中的平滑滚动，请求重画。
    void ScrollToBottom();

    // 滚动到带指定服务器 msgid 的消息行，使其进入视口顶部附近（供历史消息记录窗"定位"
    // 用）。找到并滚动成功返回 true；列表里没有该 msgid（如较早消息未加载）返回 false。
    //   msgid：目标消息的服务器 msgid；0 一律返回 false。
    bool ScrollToMsgid(long long msgid);

    // 追加一条消息 —— 新建一个 ChatBubble 挂到末尾，重新布局并滚到底部。
    // 与上一条真实消息间隔满 5 分钟或不在同一个本地日期时，先插一行时间分隔；本人刚发出、时间戳仍为 0 的回显
    // 按本机当前时间判定（翻页加载历史期间除外），消息本身的时间戳不改（docs/聊天消息时间显示方案.md 第 7 节）。
    //   msg：要追加的消息。
    void AppendMessage(const ChatMessage& msg);

    // 取当前时刻的函数：返回 Unix 毫秒。
    typedef long long (*NowMsFn)();

    /**
     *  替换消息列表取当前时刻的函数，只供单元测试固定「现在」（时间分隔行的判定与文案依赖当前时刻）。
     *  作用于进程内全部消息列表，用例结束时须传 NULL 恢复。
     *  @param fn 新的取时函数；传 NULL 恢复为读系统时钟
     */
    static void SetNowProviderForTest(NowMsFn fn);

    /**
     *  设置本列表是否显示已读回执。单聊窗建列表后调用一次开启；群聊窗不调用（默认关闭）
     *  —— 按协议群聊不做已读回执，服务端不向群成员推送 cmd_message_read_notify。
     *    b：true 开启。之后追加的每一行都继承这个设置；已有的行也一并改掉并重排。
     */
    void SetReadReceiptEnabled(bool b);

    /**
     *  应用对方的已读位点：把自己发出、已拿到服务器 msgid、且 msgid 不大于位点的消息
     *  标为已读，并记住这个位点。之后追加的行（翻页加载的历史）、之后才回填 msgid 的行
     *  （发送回执晚于已读推送到达），都按记住的位点当场判定。
     *
     *  位点只增不减：比已记住的位点小的（乱序到达的旧推送）直接忽略，已读不会改回未读。
     *    readMsgid：对方已读到的最大消息 msgid；<=0 直接忽略。
     *  返回：本次新标为已读的行数；为 0 表示没有任何变化，调用方可以不重绘。
     *  来源：cmd_message_read_notify 推送（实时）与单聊同步消息应答里的对方已读位点。
     */
    int ApplyPeerReadPosition(long long readMsgid);

    // 当前记住的对方已读位点；0 表示还不知道。
    long long PeerReadMsgid() const { return m_peerReadMsgid; }

    /**
     *  设置某个发送者（别人）的气泡头像是否画成灰度（离线用户头像置灰，2026-10-03）。宿主聊天窗
     *  在该发送者上线 / 下线时调用。本列表记住结果，已有的行当场改，之后追加的行（新消息、翻页
     *  加载的历史）在 AppendMessage 里按记住的结果设置。
     *    senderUserId：发送者 userid；<=0 直接忽略。
     *    gray：        true 画灰度（该发送者离线），false 画彩色。
     *  只作用于对方发来的行；本人的气泡由 SetSelfAvatarGray 决定。有行变化时由各行自己请求重绘。
     */
    void SetSenderAvatarGray(int64_t senderUserId, bool gray);

    /**
     *  设置本人的气泡头像是否画成灰度。宿主聊天窗在本端连接态变化时调用（掉线或正在连接时为 true）。
     *  已有的行当场改，之后追加的行按记住的结果设置。
     *    gray：true 画灰度，false 画彩色。
     */
    void SetSelfAvatarGray(bool gray);

    /**
     *  向上翻页渲染的<u>开始</u>标记。
     *
     *  用法：BeginPrependBatch() → 照常用 AppendMessage / 各类渲染入口把这一页
     *  更早的消息按时间正序灌进来 → EndPrependBatch()。中间这一段追加到末尾的行，
     *  会在 End 时被整体挪到列表头部。
     *
     *  之所以做成"标记 + 照常追加"而不是一个吃 vector 的 PrependMessages：两个
     *  聊天窗的历史渲染并不是简单地把 ChatMessage 灌进来 —— 图片消息要插"下载中"
     *  气泡并触发下载、发送人头像要按成员表解析。把那套逻辑照抄一份到翻页分支，
     *  两份迟早跑偏（@ 那次就是历史与实时各写一份漏了填 ats）。包一层标记，两条
     *  路就能共用同一段渲染代码。
     *
     *  批次期间 AppendMessage <u>不会</u>滚到底 —— 翻页时用户正盯着某处看，滚到底
     *  等于把他甩走。
     */
    void BeginPrependBatch();

    /**
     *  向上翻页渲染的<u>结束</u>标记：把本批新追加的行整体挪到列表头部，并把滚动
     *  偏移按新增高度补偿，使视口里的内容纹丝不动。
     *
     *  补偿是这件事的关键：往头部插内容会把下面所有行整体推下去，不补偿的话用户
     *  正在看的内容会突然跳走。做法是取批次前后的内容总高之差 —— 比自己逐行累加
     *  行高可靠，后者还要先把每行 Measure 一遍，容易与列表内部口径不一致。
     *
     *  本批一行都没追加时什么都不做。
     */
    void EndPrependBatch();

    // ---- 向上翻页的滚动状态查询（供宿主聊天窗判定要不要拉下一页）----
    int ScrollY() const       { return m_scrollY; }
    int ViewHeight() const    { return m_viewHeight; }
    int ContentHeight() const { return m_contentHeight; }

    // 当前滚动条滑块的矩形（宿主客户区坐标）；内容没有超出可视区时为空矩形（全 0）。
    // 供测试核对滑块位置，与绘制、命中判断用的是同一份计算。无副作用。
    RECT ScrollThumbRect() const { return ThumbRect(); }

    // ---- 平滑滚动 ----------------------------------------------------

    /**
     *  把进行中的平滑滚动推进到 nowMs 时刻：按动画曲线算出此刻的滚动位置，位置有变化
     *  就重排、重绘，并在接近顶部时通知宿主加载更早的消息。动画走完后自动停止。
     *  没有进行中的动画时什么都不做。
     *
     *  平时由逐帧驱动（DuiAnimMgr 的 60Hz 脉冲）调用；单元测试直接调用它，传一个足够
     *  靠后的时刻即可把动画推到结束。
     *    nowMs：当前时刻（毫秒），与滚轮起动画时用同一个时钟（PerfNowMs）。
     */
    void AdvanceSmoothScroll(double nowMs);

    // 是否有进行中的平滑滚动。
    bool IsSmoothScrolling() const;


    // 清空所有消息行（销毁全部 ChatBubble、滚动位置归零、重排重绘）。历史消息
    // 应答是"最近 N 条"的完整快照，渲染前先清空，避免与收到推送时乐观追加的
    // 行叠加导致重复 / 错位。
    void ClearAllMessages();

    /**
     *  按服务器 msgid 取回该行承载的消息（供宿主组装引用快照）。
     *    msgid：目标消息的服务器 msgid；0 一律返回 false。
     *    out：  [出参] 命中时填入该行的消息。
     *  @return 找到返回 true；列表里没有该 msgid（如更早未加载）返回 false。
     */
    bool GetMessageByMsgid(long long msgid, ChatMessage& out) const;

    /**
     *  按服务器 msgid 把改好的整条消息写回那一行，并重排 + 重绘。与 GetMessageByMsgid 成对使用，
     *  用于接龙卡片收到变更推送、到了截止时间之后的刷新。
     *  <u>必须重排而不能只重绘</u>：接龙卡片的高度随接龙条数与有无按钮变化，理由同 SetFolderMessage。
     *    msgid：目标消息的服务器 msgid；0 一律返回 false。
     *    msg：  改好的整条消息（通常是 GetMessageByMsgid 取出后改过的那份）。
     *  @return 找到并写回返回 true；列表里没有该 msgid 返回 false。
     */
    bool SetMessageByMsgid(long long msgid, const ChatMessage& msg);

    /**
     *  取列表里某一类消息的服务器 msgid，按列表顺序排列，msgid 为 0 的行（发送中等）不在其中。
     *  用于成员表到达后逐张刷新接龙卡片上的名字。
     *    kind：消息类型，取值见 ChatMockData.h 的 ChatMsgKind。
     *  @return msgid 列表；没有这类消息时为空。本方法只读。
     */
    std::vector<long long> GetMsgidsOfKind(int kind) const;

    // 列表里是否已存在带该服务器 msgid 的图片气泡（收到 live 图片时去重用，避免与
    // 历史已渲染的同一条重复 / 错位）。msgid 为 0 一律返回 false。
    bool HasImageMsgid(long long msgid) const;

    // ---- 发图气泡的发送态更新（按 clientMsgId 定位本端"发送中"的图片气泡）----

    // 更新某条图片消息的上传进度（0-100）并置"上传中"态。clientMsgId 找不到则忽略。
    void UpdateImageUploadProgress(const std::string& clientMsgId, int percent);

    // 把某条图片消息标为"发送失败"态。clientMsgId 找不到则忽略。
    void MarkImageUploadFailed(const std::string& clientMsgId);

    // 把某条图片消息置为"正常"态（上传完成）。clientMsgId 找不到则忽略。
    void MarkImageUploadDone(const std::string& clientMsgId);

    /**
     *  上传成功后，把服务端返回的 fileid / md5 回填到本端那条图片气泡上。
     *
     *  本端发出的图，气泡是在<u>上传之前</u>就插进列表的，那时这两项还不存在。它们
     *  只影响一件事：这条消息将来被引用时，要写进 content.quote 供收方画缩略图 ——
     *  不回填的话，引用自己发的图永远带不出定位信息，对方的引用块只有一块空白。
     *    clientMsgId：本端发送时生成的客户端消息ID；空串一律忽略。
     *    fileid / md5：服务端返回的图片文件ID与内容MD5。
     */
    void SetImageIdsByClientMsgId(const std::string& clientMsgId,
                                  const CString& fileid, const CString& md5);

    // ---- 收到图片的下载态更新（按服务器 msgid 定位"下载中"的图片气泡）----

    // 某条收到的图片下载完成：按 msgid 找到本行，填本地缓存路径并渲染。找不到则忽略。
    void SetImageDownloadedByMsgid(long long msgid, const CString& localPath);

    // 某条收到的图片下载失败：按 msgid 找到本行标失败。找不到则忽略。
    void MarkImageDownloadFailedByMsgid(long long msgid);

    /**
     *  一段语音下载有了结果：把列表里所有该 md5 的语音气泡改为就绪（记下本地路径）或下载失败（语音消息第 3d-3 步）。
     *  按 md5 而不按 msgid 定位：同一段语音被转发多次时会有好几条消息共用一个文件，下载一次即可全部就绪。
     *  只改「下载中」「下载失败」的气泡：「发送中」的本人语音与已就绪的气泡不受影响。
     *  @param md5       语音文件内容 MD5（小写十六进制）；空串一律忽略
     *  @param ok        是否下载并校验成功
     *  @param localPath 成功时文件的本地完整路径（ChatVoice\<md5>.ogg）；失败时忽略
     *  @return 改动了几条气泡；有改动时重绘列表
     */
    int ApplyVoiceDownloadResult(const std::string& md5, bool ok, const CString& localPath);

    /**
     *  重发一条发送失败的本人语音（语音消息第 3c 步）：按 clientMsgId 找到那条气泡，改回「发送中」、清掉失败原因，
     *  位置不变。群聊里失败原因那一行随之去掉、行高变矮，故更新后重排。
     *  @param clientMsgId 发送时的客户端消息ID；空串一律忽略
     *  @return 找到一条发送失败的本人语音并改回发送中时返回 true；没有这条、或它不是失败状态时返回 false 且不改
     */
    bool RestartVoiceSendByClientMsgId(const std::string& clientMsgId);

    /**
     *  重发一条被服务端拒绝的本人文件消息（2026-10-05，群里经「发送到会话」发出的录像等）：文件已在服务器上，不再上传，
     *  只重发文件消息。按 fileClientMsgId 找到那条气泡，清掉失败标记与原因、状态回到已完成（可以打开），位置不变；
     *  群聊里失败原因那一行随之去掉、行高变矮，故更新后重排。
     *  @param clientMsgId 发送时的客户端消息ID；空串一律忽略
     *  @return 找到一条发送失败的本人文件消息并改回已完成时返回 true；没有这条、它不是本人的或不是失败状态时返回 false 且不改
     */
    bool RestartFileMessageSendByClientMsgId(const std::string& clientMsgId);

    // ---- 语音气泡的播放（语音消息第 3e 步）。定位一条语音：msgid 大于 0 时按 msgid，否则按 clientMsgId ----

    /**
     *  让一条语音进入或退出播放中。进入时同一列表里别的正在播的先停下，这一条记为已听（红点消失）。
     *  @param msgid       服务器消息ID；还没回执的本人语音为 0
     *  @param clientMsgId 客户端消息ID；msgid 为 0 时据它定位
     *  @param playing     true 进入播放中，false 退出（图标回到第一帧）
     *  @return 找到这条语音时返回 true
     */
    bool SetVoicePlaying(long long msgid, const std::string& clientMsgId, bool playing);

    /**
     *  更新播放中喇叭图标的帧号；帧号没变时不重绘。
     *  @param msgid、clientMsgId 定位方式同 SetVoicePlaying
     *  @param frame 帧号（VoicePlayFrame 的结果）
     *  @return 找到这条语音时返回 true
     */
    bool SetVoicePlayFrame(long long msgid, const std::string& clientMsgId, int frame);

    // 让列表里所有正在播放的语音退出播放中（窗口关闭、开始录音、播放被别的窗口顶掉时）。
    void StopAllVoicePlaying();

    /**
     *  一条语音播完后，取紧挨着的下一条自动接着播的语音（规则见 PickNextAutoPlayVoice）。
     *  @param msgid、clientMsgId 刚播完的那条，定位方式同 SetVoicePlaying
     *  @param next [出参] 找到时填下一条的副本
     *  @return 有下一条可以接着播时返回 true
     */
    bool NextAutoPlayVoice(long long msgid, const std::string& clientMsgId, ChatMessage& next) const;

    /**
     *  下载失败的语音重新下载：把该 md5 的「下载失败」气泡改回「下载中」。
     *  @param md5 语音文件内容 MD5；空串一律忽略
     *  @return 改动了几条
     */
    int MarkVoiceDownloading(const std::string& md5);

    /**
     *  别的窗口（另一个聊天窗或消息记录窗）里听过一条语音后，把本列表里同一条的未听红点去掉（语音消息第 6a 步）。
     *  只改对方发来、还没听过的那一条语音；本人发的、已听的、不是语音的消息都不动。
     *  @param msgid 服务器消息ID；小于等于 0 时忽略
     *  @return 找到这条未听的语音并改为已听时返回 true（该气泡重绘），否则返回 false
     */
    bool MarkVoicePlayedByMsgid(long long msgid);

    // ---- 文件气泡的收发态更新 -----------------------------------------
    // 注：文件气泡高度随状态变化，故改状态的接口在更新后会 ForceLayout 重排（进度类
    // 更新不改高度，只 Invalidate）。clientMsgId / msgid 找不到则忽略。

    // 列表里是否已存在带该服务器 msgid 的文件气泡（收到 live 文件去重）。0 返回 false。
    bool HasFileMsgid(long long msgid) const;

    // 发送方：按 clientMsgId 定位本端"发送中"的文件气泡。
    void UpdateFileUploadProgress(const std::string& clientMsgId, int percent);
    void MarkFileUploadFailed(const std::string& clientMsgId);
    void MarkFileUploadDone(const std::string& clientMsgId);
    void MarkFileCancelledByClientMsgId(const std::string& clientMsgId);
    // 用户点「重新发送」并已重新入队上传：把该条置「上传中」态、进度归零。
    void MarkFileUploadStartedByClientMsgId(const std::string& clientMsgId);

    // 接收方：按服务器 msgid 定位文件气泡。
    void UpdateFileDownloadProgress(long long msgid, int percent);
    /**
     *  文件下载完成：把 fileMsgid 为 msgid 的文件气泡置为完成态并填上本地路径。
     *    msgid：    服务器消息ID；为 0 时直接返回 false。
     *    localPath：下载落地的本地完整路径。
     *    incoming： [出参，可为 NULL] 命中时写入该文件是否为对方发来（true）、自己发出（false）；
     *               未命中时不修改。单聊窗据此只对对方发来的文件上报「已接收」回执。
     *  @return 本列表里有这条文件气泡、已回填时返回 true；没有时返回 false，列表不变。
     *          下载结果按 msgid 广播给所有聊天窗，绝大多数窗口都会得到 false。
     */
    bool SetFileDownloadedByMsgid(long long msgid, const CString& localPath, bool* incoming = NULL);
    void MarkFileDownloadFailedByMsgid(long long msgid);
    void MarkFileCancelledByMsgid(long long msgid);
    // 用户点「拒绝」：把该条置「已拒绝接收」态（仍可再点「重新下载」反悔）。
    void MarkFileRejectedByMsgid(long long msgid);
    // 用户点「接收」/「重新下载」并已发起下载：把该条置「下载中」态、进度归零。
    void MarkFileDownloadStartedByMsgid(long long msgid);
    void MarkFileDeletedByMsgid(long long msgid);
    // 发送方：更新"对端把这条文件收到什么程度了"（按 msgid 定位本端发出的那条气泡）。
    //   peerState：取值见 ChatFilePeerState。
    // 返回是否命中了某条气泡 —— 调用方据此把"本窗口没有这条消息"与"更新了但没生效"
    // 区分开，这两类问题的排查方向完全不同。
    bool SetFilePeerStateByMsgid(long long msgid, int peerState);

    // ---- 文件夹气泡（kMsgFolder）的状态更新 ---------------------------
    //
    // 传输<u>进行中</u>的文件夹气泡是纯本地的：不走 cmd_send_message、没有服务器 msgid，
    // 定位一律靠<u>会话 id</u>。发起之初用的是本地占位 id，服务端分配了真会话 id 之后换成
    // 后者（见 CBalloonChatWindow::OnFolderBound）。
    // 更新分成"取出来改—写回去"两步而不是给每种状态各开一个接口：文件夹传输的状态字段有
    // 十来个（状态 / 进度 / 当前文件 / 序号 / 失败原因 / 落地目录…），一次事件往往要改好几个，
    // 逐个开接口只会让宿主分不清哪些该一起改。
    //
    // 传输走到终态后，服务端会在双方会话里落一条 msgtype=18 的传输记录条推下来，聊天窗
    // 用它<u>就地接管</u>那条本地气泡（填上 msgid 与服务端时间戳、切到对应终态），界面上
    // 因而始终只有一条、位置也不跳。接管之后那条气泡就带上了 msgid，本组的两个接口<u>不再
    // 认它</u>——见下面各自的说明。

    /**
     *  按会话 id 取回<u>尚未被服务端记录接管</u>的那条文件夹气泡（即 msgid 仍为 0 的
     *  本地气泡）。
     *
     *  为什么要把带 msgid 的排除在外：同一次传输可能留下<u>多条</u>服务端记录（每次尝试
     *  各一条：先失败落一条，保留期内重试成功再落一条），它们的会话 id 完全相同。若不加
     *  这道限制，第二条记录就会把第一条那个已经定格的气泡再改一遍；翻历史时同一会话的
     *  多条记录一起渲染进来，实时进度也会更新到最早那条上去。
     *
     *    sessionId：本次传输的会话 id（UTF-8）；空串一律返回 false。
     *    out：      [出参] 命中时填入该行的消息。
     *  @return 找到返回 true；列表里没有这条会话的<u>本地</u>气泡（从未有过、或已被记录
     *          接管）返回 false。
     */
    bool GetFolderMessage(const std::string& sessionId, ChatMessage& out) const;

    /**
     *  按直传会话 id 或本端的 clientMsgId 取回一条在线直传的文件气泡（2026-10-04 起）。发送方的气泡在 1810 应答到达
     *  之前只有 clientMsgId，之后两者都有；接收方的气泡只有会话 id。同一会话有多条时取最后一条。
     *    key： 直传会话 id 或 clientMsgId（UTF-8）；空串一律返回 false。
     *    out： [出参] 命中时填入该行的消息。
     *  @return 找到返回 true；改为离线文件之后的气泡（fileDirect 为 false）不再认
     */
    bool GetDirectFileMessage(const std::string& key, ChatMessage& out) const;

    /**
     *  按与 GetDirectFileMessage 相同的规则写回一条在线直传的文件气泡，并重排 + 重绘（文件卡片的高度随状态变化）。
     *    key： 定位用的会话 id 或 clientMsgId（写回之前那条气泡上的）。
     *    msg： 改好的整条消息。
     *  @return 命中并写回返回 true；找不到返回 false
     */
    bool SetDirectFileMessage(const std::string& key, const ChatMessage& msg);

    /**
     *  按会话 id 把改好的消息写回那条<u>尚未被记录接管</u>的文件夹气泡，并重排 + 重绘。
     *  命中条件与 GetFolderMessage 完全一致（会话 id 相同且 msgid 仍为 0），两者成对使用。
     *
     *  <u>必须重排而不能只重绘</u>：文件夹气泡的高度随状态变化（有没有进度条、有没有
     *  失败原因那一行、底部有几个按钮），只 Invalidate 的话新出现的按钮会被裁在气泡外面、
     *  点不到，看上去就是"点了没反应"。
     *
     *    sessionId：定位用的会话 id（UTF-8，即写回<u>之前</u>那条气泡上的那个）。
     *    msg：      改好的整条消息。msgid 可以在本次写回时一并填上——那正是"接管"这一步，
     *               写回之后本方法与 GetFolderMessage 就都不再认这条气泡了。
     *  @return 命中并写回返回 true；找不到返回 false。
     */
    bool SetFolderMessage(const std::string& sessionId, const ChatMessage& msg);

    /**
     *  把某条<u>尚未被记录接管</u>的文件夹气泡整行移除（销毁该 ChatBubble 并重排）。
     *  命中条件与 GetFolderMessage 完全一致（会话 id 相同且 msgid 仍为 0）。
     *
     *  用在"这一笔根本没能开始"的场合：发送端点了「发送文件夹」之后气泡是先建出来
     *  的（用户要立刻看到反馈），随后才收到服务端的邀请应答。若服务端因对方不在线
     *  直接拒绝，这一笔连会话都没建立、服务端不会留下任何消息记录，此时把气泡改成
     *  失败态并不合适 —— 聊天记录里会平白多出一条"发送失败"，而实际上什么都没发生。
     *  这种情形应当移除气泡、另给用户一句提示。
     *
     *    sessionId：本次传输的会话 id（UTF-8，此时通常还是本地占位 id）；空串一律
     *               返回 false。
     *  @return 找到并移除返回 true；没有这条会话的本地气泡返回 false。
     */
    bool RemoveFolderMessage(const std::string& sessionId);

    // ---- 密聊邀请卡片（kMsgSecretInvite，docs/密聊方案.md 4.8）。卡片是纯本地气泡（msgid 为 0），按密聊会话号定位；
    //      同一会话号在列表里最多一张 ----

    /**
     *  按密聊会话号取回那张邀请卡片。
     *    sessionId：密聊会话号；0 一律返回 false。
     *    out：      [出参] 命中时填入该行的消息。
     *  @return 找到返回 true
     */
    bool GetSecretInviteMessage(unsigned long long sessionId, ChatMessage& out) const;

    /**
     *  按密聊会话号写回那张邀请卡片。
     *    sessionId：定位用的会话号（写回之前那张卡片上的）。
     *    msg：      改好的整条消息。
     *    relayout： true 时重排整个列表（按钮数变了、卡片高度可能变化）；只改了剩余秒数时传 false，只重绘这一行。
     *  @return 命中并写回返回 true
     */
    bool SetSecretInviteMessage(unsigned long long sessionId, const ChatMessage& msg, bool relayout);

    /**
     *  按密聊会话号移除那张邀请卡片（销毁该行并重排）。同意、忽略、超时、撤回、在其他设备处理之后调用。
     *    sessionId：密聊会话号；0 一律返回 false。
     *  @return 找到并移除返回 true
     */
    bool RemoveSecretInviteMessage(unsigned long long sessionId);

    /**
     *  取列表里全部密聊邀请卡片的会话号（按从上到下的顺序）。倒计时定时器据此逐张刷新剩余秒数。
     */
    std::vector<unsigned long long> GetSecretInviteSessionIds() const;

    /**
     *  打开或关闭密聊限制：打开后整张列表没有右键菜单、不响应 Ctrl+C、不能进入多选、气泡不能拖出；正在多选时立即退出。
     *  单聊窗进入密聊状态（密聊中、只能查看）时打开，回到普通状态时关闭。
     *  @param on 是否打开
     */
    void SetSecretRestricted(bool on);

    /**
     *  当前是否处于密聊限制（气泡据此不弹菜单、不准备拖出）。
     */
    bool IsSecretRestricted() const { return m_secretRestricted; }

    /**
     *  清除全部密聊内容：先把每条密聊文字在内存里清零，再移除这些行与全部密聊邀请卡片并重排（本人退出密聊、关窗时调用）。
     *  图片字节在 ChatImageCache 的内存图源里，由宿主另行撤销。
     *  @return 移除的行数
     */
    size_t WipeSecretMessages();

    /**
     *  按 clientMsgId 取回一条消息（密聊图片收齐后据此改写那条占位气泡）。
     *    clientMsgId：本地消息号；空串一律返回 false。
     *    out：        [出参] 命中时填入该行的消息。
     *  @return 找到返回 true
     */
    bool GetMessageByClientMsgId(const std::string& clientMsgId, ChatMessage& out) const;

    /**
     *  按 clientMsgId 写回一条消息并重排、重绘（图片收齐后尺寸可能与占位不同）。
     *  @return 命中并写回返回 true；空串或找不到时返回 false
     */
    bool SetMessageByClientMsgId(const std::string& clientMsgId, const ChatMessage& msg);

    /**
     *  把某条已被服务端记录接管的文件夹气泡放回"活的本地气泡"状态：清掉它身上的 msgid
     *  与消息时间，使 GetFolderMessage / SetFolderMessage 重新认得它。
     *
     *  用在<u>重试续传</u>这一步：一笔传输失败后服务端已经落过一条 result=7 的记录、把
     *  气泡接管了；用户点「重试」、传输重新开始时，这条气泡要重新变成活的，本次尝试
     *  结束时到达的那条新记录才能再接管它。不这么做的话，重试期间的进度更新一条也落不
     *  到气泡上（表现为点了重试界面纹丝不动）。
     *
     *    sessionId：本次传输的会话 id（UTF-8）；空串一律返回 false。
     *  @return 找到并清掉返回 true；列表里没有这条会话的气泡返回 false。
     *          同一会话有多条气泡时（翻历史把这一笔早先几次尝试的记录也拉了进来）取
     *          <u>最后</u>那条——它才是刚被接管、马上要复用的那一条，更早的几条属于
     *          已经过去的尝试，不该被改动。
     */
    bool ReviveFolderMessage(const std::string& sessionId);

    // ---- 真图头像下载完成后刷新气泡 ----------------------------------

    // 把所有"发送者为 userId"的气泡的头像真图换成 imagePath（真图下载完成后由聊天窗
    // 调用，让历史已渲染的该发送者气泡也显示真图）。userId 为 0 一律忽略。
    void RefreshSenderAvatar(int64_t userId, const CString& imagePath);

    // ---- 多选模式 -----------------------------------------------------

    // 当前是否处于多选模式。
    bool IsMultiSelectMode() const { return m_multiSelect; }

    // 进入 / 退出多选模式。进入后底部让出操作条、各行显示勾选圈；退出
    // 时清空所有行的选中态。会触发一次重排 + 重绘。
    void SetMultiSelectMode(bool on);

    /**
     *  查询某 userid 是否仍在本会话（群）的成员表里。
     *    userdata：SetMemberInGroupQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 仍在群里返回 true。
     */
    typedef bool (*MemberInGroupQuery)(void* userdata, long long uid);

    /**
     *  装上"成员是否仍在群里"的查询钩子。气泡里 @某人 的右键菜单据此决定
     *  「再 @ 一次 TA」要不要置灰 —— 人都退群了，再 @ 也送不到。
     *  只有群聊窗需要装；单聊窗不装即可（不装时一律视为在群，见 IsMemberInGroup）。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetMemberInGroupQuery(MemberInGroupQuery fn, void* userdata);

    /**
     *  该成员是否仍在本会话的成员表里。未装查询钩子时恒返回 true ——
     *  单聊窗、以及调试入口都属于"无从查证"，此时不该平白把菜单项置灰。
     */
    bool IsMemberInGroup(long long uid) const;

    /**
     *  查询某 userid 在本群的角色。
     *    userdata：SetMemberRoleQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 网络侧角色取值（0 成员 / 1 管理员 / 2 群主）；不在群或查不到返回负值。
     */
    typedef int (*MemberRoleQuery)(void* userdata, long long uid);

    /**
     *  装上"查成员在本群的角色"的查询钩子。气泡里 @某人 的右键菜单据此决定
     *  末尾要不要给出「设为管理员 / 撤销管理员」。只有群聊窗需要装。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetMemberRoleQuery(MemberRoleQuery fn, void* userdata);

    /**
     *  该成员在本群的角色，取值见 grouprole::NetGroupRole。
     *  未装查询钩子时返回 -1（无从查证），此时管理项一律不出现。
     */
    int MemberNetRole(long long uid) const;

    /**
     *  查询某 userid 当前是否被本人「特别关注」。
     *    userdata：SetStarredQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 已关注返回 true。
     */
    typedef bool (*StarredQuery)(void* userdata, long long uid);

    /**
     *  装上"该联系人是否被特别关注"的查询钩子。气泡里 @某人 的右键菜单、以及发送者
     *  头像的右键菜单据此决定那一项的文案是「特别关注」还是「取消特别关注」。
     *
     *  为什么要走钩子：提醒模式表只有主面板那一份（balloonmain::NotifyModeModel），气泡与
     *  消息列表都不认识它；与上面两个群成员查询同一套路子，宿主窗代问、控件只管显示。
     *  只有群聊窗需要装；单聊窗不装即可（不装时恒返回 false，见 IsStarred）。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetStarredQuery(StarredQuery fn, void* userdata);

    /**
     *  该联系人当前是否被本人特别关注。未装查询钩子时恒返回 false（无从查证时
     *  按"未关注"显示，点下去就是去关注，是这两种误判里代价较小的那个）。
     */
    bool IsStarred(long long uid) const;

    /**
     *  装上「本人是否在本群关注了 TA 的发言」的查询钩子（docs/消息提醒模式与消息盒子方案.md §12）。
     *  气泡里 @某人 与发送者头像的右键菜单据它决定「关注 TA 在本群的发言 / 取消关注」的文案。
     *  签名与特别关注的钩子相同；只有群聊窗需要装，不装时 IsGroupFollowed 恒返回 false。
     *    fn：      查询回调；传 NULL 表示卸载。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetGroupFollowQuery(StarredQuery fn, void* userdata);

    /**
     *  本人是否在本群关注了该成员的发言。未装查询钩子时恒返回 false。
     */
    bool IsGroupFollowed(long long uid) const;

    /**
     *  设置 / 读取本人在本群的角色，取值见 grouprole::NetGroupRole，负值表示未知。
     *  由群聊窗在自己的角色更新时同步下来 —— 气泡不认识"当前登录用户"这类业务对象，
     *  与其让它去问，不如由宿主推给它。
     */
    void SetMyNetRole(int role);
    int  MyNetRole() const;

    /**
     *  设置 / 读取文字消息右键菜单里「添加到备忘」的出现方式（docs/备忘与提醒方案.md §8），取值见
     *  ChatMemoMenuMode。单聊窗设为 kChatMemoMenuSingle；群聊窗按本人能否新建群备忘设为 kChatMemoMenuGroupMine
     *  或 kChatMemoMenuGroupBoth，角色变化时重设。其它复用本控件的窗口保持默认的 kChatMemoMenuNone。
     */
    void SetMemoMenuMode(int mode);
    int  MemoMenuMode() const;

    // ---- ChatBubble 右键菜单 / 多选点击回调进来的请求 -----------------

    /**
     *  删除某一条消息行（气泡右键「删除」）。
     *
     *  删除是本人视角的真删除（docs/清理聊天记录方案.md §10），按该行有没有服务器 msgid 分两种：
     *    · msgid 非正（发送中、发送失败，服务器上没有这条）：直接从列表移除并销毁该 ChatBubble，
     *      因此孤立的时间分隔行一并移除、时间行的判定基准按剩下的行重算（2026-10-14），随后重排；
     *    · msgid 为正：本方法<u>不</u>就地改界面，只把 msgid 经 WM_CHAT_DELETE_REQUEST 投给
     *      宿主聊天窗，由宿主确认后发 cmd_delete_message；气泡等服务端推来
     *      cmd_messages_deleted_notify 后走 RemoveMessagesByMsgid 移除。
     *
     *    bubble：目标消息行；NULL 时什么都不做。
     *  @return 已就地移除（bubble 已销毁，调用方其后不得再访问它）返回 true；
     *          转交宿主或 bubble 为 NULL 时返回 false，bubble 仍然有效。
     */
    bool RequestDeleteBubble(ChatBubble* bubble);

    /**
     *  删除多选模式下选中的全部消息行（多选操作条上的「删除 N」）。
     *
     *  与 RequestDeleteBubble 同一口径：选中行里 msgid 非正的立即移除；msgid 为正的留在列表里，
     *  它们的 msgid 一次性经 WM_CHAT_DELETE_REQUEST 投给宿主，等删除推送到达后再移除。立即移除时同样清理孤立的
     *  时间分隔行、重算判定基准。无论选中行是哪一种，都会退出多选模式并重排。未处于多选模式或一行都没选中时什么都不做。
     *
     *  @return 投给宿主、等待服务端删除的 msgid（按列表顺序）；没有这类行时返回空数组。
     *          列表不在窗口里（单元测试）时照样返回，只是投递这一步会跳过。
     */
    std::vector<long long> DeleteSelected();

    /**
     *  取多选模式下选中的、可以转发的消息 msgid（多选操作条上的「逐条转发」「合并转发」用）。
     *
     *  每一行按 CanForwardMessage 判定能否转发，msgid 按 ForwardMsgidOf 取（单聊里收到的
     *  文件行取 fileMsgid）。结果按 msgid 升序排列并去重，与服务端处理转发源的顺序一致
     *  （服务端按 msgid 升序取源消息）。本方法只读，不改选中态、不退出多选。
     *
     *  @param skipped [出参，可为 NULL] 选中了但不能转发的行数（发送中 / 发送失败、文件夹行
     *                 等），供调用方提示「已跳过 N 条不能转发的消息」
     *  @return 可转发的 msgid；未处于多选模式或没有可转发的选中行时返回空数组
     */
    std::vector<long long> GetSelectedForwardMsgids(size_t* skipped) const;

    /**
     *  同上，但按转发模式判定每一行：逐条转发按 CanForwardMessage，合并转发按 CanMergeForwardMessage
     *  （两者只差接龙卡片：合并转发收、逐条转发跳过）。上面那个不带模式的版本等同于逐条转发。
     *  这两个版本不单独处理在线直传的文件；发起转发与操作条计数用的是 CollectSelectedForward。
     *  @param merge   true 合并转发，false 逐条转发
     *  @param skipped [出参，可为 NULL] 选中了但按该模式不能转发的行数
     *  @return 可转发的 msgid，按 msgid 升序、去重
     */
    std::vector<long long> GetSelectedForwardMsgids(bool merge, size_t* skipped) const;

    /**
     *  按转发模式整理多选模式下选中的行（2026-10-05 起把在线直传的文件单独处理）。
     *  在线直传的文件服务器上没有副本，按 balloonmain::DecideDirectFileForward 处理：合并转发照常进入记录、
     *  计入 m_directPlaceholders；逐条转发本机有文件时不进 m_msgids、路径记进 m_reuploadFiles，本机没有时
     *  计入 m_skippedDirect。其余行的判定与 GetSelectedForwardMsgids 相同。本方法只读，不改选中态、不退出多选；
     *  逐条转发时要实测磁盘（见 DirectFileHasLocalCopy）。
     *  @param merge true 合并转发，false 逐条转发
     *  @return 整理结果；m_merge 等于 merge。未处于多选模式时各项都为空
     */
    ChatForwardRequest CollectSelectedForward(bool merge) const;

    /**
     *  右键「转发」的是一条本机已没有文件的在线直传文件时调用（判定见 DirectFileForwardBlocked）：不进入多选，
     *  向宿主聊天窗投一条只带 m_skippedDirect = 1 的 WM_CHAT_FORWARD_REQUEST，由宿主提示「在线直传的文件无法转发」。
     *  宿主窗口不存在时什么都不做。
     */
    void PostDirectForwardUnavailable();

    /**
     *  判断列表里是否已有同一条消息，供群聊窗对本人消息的回显去重。
     *
     *  服务端把群消息推给包括发送者在内的全体成员。本人普通发送的消息已有乐观气泡，回显要
     *  跳过；本人转发到本群、或在别的设备上发出的消息，列表里没有，回显要显示。两种情况都按
     *  本方法判断：乐观气泡在回执回填之前只有 clientmsgid，回填之后才有 msgid，故两者都比。
     *
     *  @param msgid       服务器 msgid；与某行的 msgid 或 fileMsgid 相同即命中，0 不参与比较
     *  @param clientMsgId 客户端消息ID；与某行的 clientMsgId 或 fileClientMsgId 相同即命中，
     *                     空串不参与比较
     *  @return 命中任意一行返回 true
     */
    bool ContainsMessage(long long msgid, const std::string& clientMsgId) const;

    // 请求撤回某一条消息行。本方法<u>不</u>就地改界面 —— 它只把该行的服务器 msgid
    // 经 WM_CHAT_RECALL_REQUEST 投给宿主聊天窗，由宿主连同自己的 ctype/cid 发
    // cmd_recall_message；界面改动一律等服务端推来 cmd_message_recalled_notify 后
    // 走 RecallMessageByMsgid，这样本端与对端看到的结果必然一致。
    // msgid 为 0（服务器尚未回执）的行直接忽略——右键菜单本就不会给这类行显示"撤回"。
    void RequestRecallBubble(ChatBubble* bubble);

    /**
     *  请求 Pin / 取消 Pin 某一条消息行。
     *
     *  与 RequestRecallBubble 同一套路：本方法<u>不</u>就地改界面，只把该行的服务器
     *  msgid 与目标状态经 WM_CHAT_PIN_REQUEST 投给宿主聊天窗，由宿主连同自己的
     *  ctype/cid 发 cmd_pin_message；标记改动一律等服务端推来
     *  cmd_message_pinned_notify 后走 SetPinnedByMsgid。Pin 是会话内共享的状态，
     *  这样本端与对端看到的结果必然一致。
     *  msgid 为 0（服务器尚未回执）的行直接忽略 —— 右键菜单本就不会给这类行显示该项。
     *
     *    bubble：目标消息行。
     *    pin：   目标状态，true 钉住、false 取消。
     */
    void RequestPinBubble(ChatBubble* bubble, bool pin);

    /**
     *  把带指定服务器 msgid 的消息行的 Pin 标记置上 / 清掉（收到 Pin 推送后调用）。
     *  标记画在气泡外侧、不占内容宽度也不改行高，故只重绘、不重排。
     *    msgid： 目标消息的服务器 msgid；0 一律忽略。
     *    pinned：true 钉住、false 取消。
     *  @return 找到并改了返回 true；列表里没有该 msgid（如该消息尚未加载进当前视图）
     *          时返回 false —— 这不是错误，下次翻历史时本地库里已带着标记。
     */
    bool SetPinnedByMsgid(long long msgid, bool pinned);

    /**
     *  数一数当前列表里有多少行带着 Pin 标记。
     *
     *  <u>只在没有服务端可问的时候用</u> —— 即两个调试入口（`--chatwindow` /
     *  `--groupchatwindow`）下给顶部「已 Pin N 条」提示带凑一个数出来。真实会话的
     *  权威条数是服务端 `cmd_get_pinned_messages` 应答里的 `total`：列表里只有已经
     *  加载进当前视图的那些行，被钉住的老消息往往还没翻到，照这里数会少报。
     *
     *  @return 带 Pin 标记的消息行数；列表为空时返回 0。
     */
    int CountPinned() const;

    // 把带指定服务器 msgid 的消息行就地变成居中的撤回提示行（收到撤回推送后调用）。
    //   msgid：  被撤消息的服务器 msgid；0 一律忽略。
    //   tipText：提示文案（见 balloonchat::BuildRecallTipText）。
    // 列表里没有该 msgid（如该消息尚未加载进当前视图）时安静忽略，不报错。
    // 找到并改成提示行返回 true，否则 false。
    bool RecallMessageByMsgid(long long msgid, const CString& tipText);

    /**
     *  同上，并处理撤回后重新编辑（docs/撤回后重新编辑方案.md §3.1）：撤回者是本人、这一行是本人发出的文字类
     *  消息时，在原文被提示文字覆盖之前生成快照，提示行挂上「编辑」，到期时刻为 nowMs 加 5 分钟。
     *  同一条撤回推送重复到达（重连补推）时，已经是提示行的那一行保持原样，「编辑」不受影响。
     *    msgid：        被撤消息的服务器 msgid；0 一律忽略。
     *    tipText：      提示文案。
     *    recalledByMe： 撤回者是否本人。
     *    nowMs：        撤回推送到达的时间（Unix 毫秒）。
     *    outDraftJson： [出参，可为 NULL] 生成了快照时写入快照 JSON，由宿主交给主面板存进本机库；没有生成时置空。
     *  @return 找到并改成提示行返回 true，否则 false。
     */
    bool RecallMessageByMsgid(long long msgid, const CString& tipText, bool recalledByMe, long long nowMs,
                              std::string* outDraftJson);

    /**
     *  到期处理（docs/撤回后重新编辑方案.md §3.3）：把「编辑」已到期的撤回提示行变回普通提示行并重画。
     *    nowMs：当前时间（Unix 毫秒）。
     *  @return 仍带「编辑」的行里最早的到期时刻（Unix 毫秒），宿主据此定下一次定时器；没有时返回 0。
     */
    long long ExpireRecallEdits(long long nowMs);

    /**
     *  移除带指定服务器 msgid 的消息行（收到"本人删除了消息"推送后调用，见
     *  docs/清理聊天记录方案.md §8）。
     *
     *  移除之后，若某个时间分隔行后面紧跟的是另一个时间分隔行、或已到列表末尾（即它下面的
     *  消息全被删光了），这个分隔行也一并移除，免得留下一行孤零零的时间。撤回提示行虽然也是
     *  居中样式，但它带着原消息的 msgid，属于内容而不是分隔，不会被当成孤立的分隔行。
     *  随后按剩下的行重新确定时间行的判定基准（2026-10-14）。全部移除完只重排、重绘一次。
     *
     *  @param msgids 要移除的服务器 msgid；非正的项忽略
     *  @return 实际移除的消息行数（不含顺带移除的时间分隔行）；列表里没有的 msgid 不计入
     */
    int RemoveMessagesByMsgid(const std::vector<long long>& msgids);

    // 发送回执到达后，按本端生成的 clientMsgId 找到那条"刚发出"的消息行，把服务器
    // 分配的 msgid 与消息时间回填进去。没有这一步，本端发出的气泡会一直是
    // msgid=0 / 时间=0，右键菜单里就永远不会出现"撤回"（时限判断拿不到依据）。
    //   clientMsgId： 本端发送时生成的客户端消息ID；空串一律返回 false。
    //   msgid：       服务器分配的消息ID。
    //   timestampMs： 服务器消息时间（Unix 毫秒）。
    // clientMsgId 全局唯一，故调用方可向所有已打开的聊天窗广播、由本方法自行筛选。
    // 找到并回填返回 true，本列表里没有该 clientMsgId 时返回 false（不是错误）。
    // 副作用：回填的时间使这一行与前后行的合组改变时重排、重绘列表（原来停在底部的仍停在底部）；
    // 合组不变时不重排。
    bool BackfillSentMessage(const std::string& clientMsgId, long long msgid,
                             long long timestampMs);

    /**
     *  发送回执失败（或文件在发出消息之前就上传失败）时，按本端生成的 clientMsgId 找到那条「刚发出」、
     *  还没有服务器 msgid 的消息行，置为「发送失败」并显示原因，随后重排、重绘。
     *  文字、图片、文件、卡片各类气泡都适用（文本 / 图片按 clientMsgId，文件按 fileClientMsgId 匹配）。
     *  @param clientMsgId 本端发送时生成的客户端消息ID；空串一律返回 false
     *  @param reason      失败原因（界面显示用，不带句号）
     *  @return 找到并标记返回 true；本列表没有该行、或该行已回填了服务器 msgid 时返回 false
     *          （clientMsgId 全局唯一，主面板向所有聊天窗广播，由本方法自行筛选）
     */
    bool MarkSendFailed(const std::string& clientMsgId, const CString& reason);

    // 以某一条消息为起点进入多选模式（该行预选中）。
    void EnterMultiSelectWith(ChatBubble* bubble);

    /**
     *  转发多选中的消息（多选操作条上的「逐条转发」「合并转发」）。
     *
     *  按 CollectSelectedForward 整理选中的行（在线直传的文件单独处理）；可转发的消息与要重新发送的文件
     *  一条都没有时什么都不做、也不退出多选（操作条上这两个按钮此时是灰的）。否则把整理结果经
     *  WM_CHAT_FORWARD_REQUEST 投给宿主聊天窗，随后退出多选。消息列表不直接碰网络，
     *  与删除、撤回同一做法。列表不在窗口里（单元测试）时投递这一步跳过。
     *
     *  @param merge true 合并转发，false 逐条转发
     *  @return 投给宿主（或本应投给宿主）的条数：msgid 条数加要重新发送的文件个数；0 表示没有可转发的行、未做任何事
     */
    size_t RequestForwardSelected(bool merge);

    // 引用回复某一条消息（mock 阶段为占位提示，真实引用待接入）。
    void RequestReplyBubble(ChatBubble* bubble);

    // 多选模式下某行选中态变化后由 ChatBubble 回调 —— 刷新底部操作条
    // 上的计数显示。
    void NotifySelectionChanged();

    /**
     *  清掉除 keep 之外所有气泡的<u>文字选区</u>（与多选勾选无关）。
     *  同一时刻只允许一个气泡有文字选区 —— 否则会出现两条消息同时高亮、而"复制"
     *  只取其中一条的割裂感。在某个气泡上开始新选择、或点到空白处时调用。
     *    keep：要保留选区的气泡；传 NULL 表示全部清掉。
     */
    void ClearTextSelectionExcept(ChatBubble* keep);

    /**
     *  取当前有文字选区的那个气泡里选中的文本（表情已替换成 "[表情]"）。
     *  没有任何气泡有选区时返回空串。供宿主聊天窗响应 Ctrl+C。
     */
    CString GetSelectedText() const;

    /**
     *  把当前选中的那段内容复制到剪贴板（内容口径与气泡右键「复制」完全一致）：同时写
     *  纯文字（内联表情替换成 "[表情]"）与 Flamingo 自己的格式（粘到聊天输入框时表情能
     *  还原成表情，见 ChatClipboardIo.h）。
     *
     *  供宿主聊天窗响应 Ctrl+C。<u>没有任何气泡有选区时什么都不做</u> —— 不去
     *  "退而复制整条"，那种情形下"整条"指的是哪一条说不清楚。
     *  @return 确实有选区并已写入剪贴板返回 true；没有选区返回 false（调用方据此
     *          决定要不要把这次按键交回原处理者，比如输入框自己的复制）。
     */
    bool CopySelectedTextToClipboard();

    /**
     *  让所有消息行重新测量并重绘。
     *
     *  用于"消息内容没变、但渲染口径变了"的场合 —— 目前只有系统设置里改动
     *  「自动识别链接」开关时用得上。行高可能随之变化（链接不改变文字，理论上
     *  不变；但重新测量是安全的做法），故走一次完整重排而不是只 Invalidate。
     */
    void RefreshAllRows();

    // DuiControl 覆写：按各 ChatBubble 的行高堆叠子控件（含滚动偏移）。
    void Layout(const RECT& rcAvail) override;

    // DuiControl 覆写：绘制消息行 + 右侧细滚动条 +（多选时）底部操作条。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：滚轮滚动消息列表。只更新平滑滚动的目标（delta 按比例换算成像素，
    // 一格 64 像素），位置由逐帧驱动在约 150 毫秒内过渡过去，所以返回时 ScrollY 尚未变化。
    // 可滚动范围为零时也消费滚轮（返回 true），不再向外层冒。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

    // DuiControl 覆写：左键点击 —— 命中滚动条滑块则开始拖动；否则多选模式下
    // 命中底部操作条执行 取消 / 逐条转发所选 / 合并转发所选 / 删除所选。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：鼠标移动 —— 正在拖动滚动条滑块时据 pt.y 更新滚动位置；否则报告鼠标是否在
    // 滚动条命中带上（悬停时滑块淡入并保持可见）。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写（2026-10-04 起）：鼠标离开本控件时滑块开始淡出。返回值同基类。
    bool OnMouseLeave() override;

    // DuiControl 覆写：左键抬起 —— 结束滚动条滑块拖动。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    /**
     *  DuiControl 覆写：把滚动条命中带的点击优先判给本控件。
     *
     *  基类的命中测试是"子控件优先"，而消息行（ChatBubble）的矩形横向占满整个
     *  列表宽度、把右侧滚动条整个盖在下面，于是点击滚动条会先命中气泡、被气泡
     *  当成"开始拖拽选中文字"消费掉，滚动条就再也点不动了。消息把可视区填满时
     *  （长文本）尤其明显 —— 列表下方连一块没有行覆盖的空白都不剩。
     *  故这里在下探子控件之前先截下滚动条命中带。
     *    ptHostClient：落点（host 客户区坐标）。
     *  @return 落在滚动条命中带内时返回 this；否则退回基类的逐层命中。
     */
    balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;

private:
    // 逐帧驱动与它的完成回调（都定义在 ChatMessageList.cpp）走完一段时要回调本列表，
    // 需要访问下面的私有方法。
    friend class ChatScrollTicker;
    friend struct ChatScrollTickerDone;

    // 确保平滑滚动的逐帧驱动在运行：没有在运行就向 DuiAnimMgr 挂一个。同一时刻只挂一个，
    // 避免一个脉冲里重复重排。
    void EnsureScrollTicker();

    // 逐帧驱动走完一段（一个动画时长）时调用：动画还没结束（期间又收到了滚轮）就再挂一段。
    void OnScrollTickerDone();

    // 滚动条滑块矩形（host 客户区坐标）；内容不溢出（无滚动条）时返回空矩形。
    RECT ThumbRect() const;

    // 只把滚动位置置为"滚到底部"的占位值、取消进行中的平滑滚动，不排版也不重画；由调用方
    // 随后的排版夹紧成真实位置。AppendMessage 紧接着就要排版，用它免得每追加一条排两遍。
    void RequestScrollToBottom();

    /**
     *  落点是否位于滚动条的命中带内。命中带 = 滑块所在竖直窄带（左右各放宽
     *  kBarMargin 便于点中）与消息行区的交集；没有滚动条时恒为 false。
     *  供 HitTest 与 OnLButtonDown 共用，避免两处判断随时间漂移。
     *    pt：落点（host 客户区坐标）。
     *  @return 位于命中带内返回 true。
     */
    bool HitScrollBarBand(POINT pt) const;

    // 据滑块顶部目标 y（host 客户区坐标）反推并设置滚动偏移 m_scrollY，随后重排重绘。
    //   thumbTopY：期望的滑块顶部 y。
    void ScrollToThumbTop(int thumbTopY);

    // 消息行区域（去掉多选操作条后的可视矩形）。
    RECT RowsViewRect() const;

    // 多选操作条矩形（非多选模式返回空矩形）。
    RECT MultiSelectBarRect() const;

    // 统计当前选中的消息条数。
    int  SelectedCount() const;

    // 取本列表所属窗口的 HWND（用于弹消息框 / 操作剪贴板）。
    HWND HostHwnd() const;

    // 把要删除的服务器 msgid 经 WM_CHAT_DELETE_REQUEST 投给宿主聊天窗。
    //   msgids：要删除的服务器 msgid，调用方保证均为正数；为空时什么都不做。
    // 宿主窗口无效（单元测试里列表不在窗口中、或窗口已销毁）时不投递，也不泄漏。
    void PostDeleteRequest(const std::vector<long long>& msgids);

    // 按需(重)建离屏合成缓冲，使其至少 w×h 大；ref 提供位图色彩格式。
    void EnsureOffscreen(HDC ref, int w, int h);

    // 释放离屏合成缓冲。
    void ReleaseOffscreen();


private:
    // 滚动到接近顶部时向宿主聊天窗投 WM_CHAT_LOAD_OLDER，请它判断要不要拉更早的
    // 一页。本控件不持有分页状态（有没有在途、还有没有更早的、before 锚点是多少），
    // 那些属于会话、归宿主窗管，故这里只报告"用户滚到顶部附近了"这一个事实。
    void NotifyMaybeLoadOlder();

    // 向上翻页批次的现场：批次开始时的行数与内容总高。EndPrependBatch 据此算出
    // 本批追加了多少行、上方长高了多少。m_prependActive 同时用来抑制批次期间
    // AppendMessage 的"滚到底"。
    bool   m_prependActive;
    size_t m_prependMarkCount;
    int    m_prependMarkHeight;

    // 上一条<u>真实</u>消息的时间（Unix 毫秒），供时间分隔行判定用；0=还没有过。
    // 只记真实消息：分隔行自身不参与判定，时间未知（0）的消息也不更新它，否则
    // 后面一条正常消息会与 0 相比、被判成"没有上一条"而多插一行。
    // 本人回显先记本机时间，回执回填时若它仍是最后一条真实消息，换成服务端时间（2026-10-14）。
    long long m_lastMsgTimestampMs;

    // 当前时刻（Unix 毫秒）：默认读系统时钟，测试经 SetNowProviderForTest 替换。
    static long long NowMs();

    // 时间分隔行的文字最近一次按哪一天格式化（本地日期，YYYYMMDD）；0 表示还没有格式化过。
    // OnPaint 发现本地日期与它不同时调 RefreshTimeSeparatorTexts，「今天」「昨天」随日期变化（2026-10-14）
    int m_separatorDayKey;

    // 本地日期变了（或第一次绘制）时，按当前时刻重新格式化全部时间分隔行的文字；文字有变化的行重画
    void RefreshTimeSeparatorTexts();

    // 插入一行时间分隔：标注 markMs 那条消息的时间，文字按 nowMs 格式化；文字为空（时间不可用）时不插
    void AppendTimeSeparatorRow(long long markMs, long long nowMs);

    // 第 index 个子控件之后是否不再有真实消息（时间行、撤回提示等 kMsgTime 行不算）。
    // BackfillSentMessage 据此判断回填的这条是不是消息流末尾那条，是才更新时间行基准
    bool IsLastRealMessage(size_t index) const;

    // 按第 index 个子控件当前的消息内容重新判定它与前后两行的合组（口径同 Layout），结果与它记着的
    // IsGroupedWithPrev / IsGroupedWithNext 不同时返回 true。BackfillSentMessage 回填了时间后据此决定要不要重排。
    // index 越界或该行不是消息行时返回 false。无副作用
    bool GroupingChangedAt(size_t index) const;

    // 移除孤立的时间分隔行：后面紧跟另一个分隔行、或已到列表末尾的那些（它下面的消息都已移除）。
    // 撤回提示行带着原消息的 msgid，不算分隔行。只移除、不重排，调用方随后自行重排
    void RemoveOrphanTimeSeparators();

    // 移除消息行之后，按列表里剩下的行重新确定 m_lastMsgTimestampMs（2026-10-14）：否则基准停在被删的那条上，
    // 紧接着追加的消息与它比较，可能漏插时间行
    void RecomputeLastMsgTimestamp();

    // 向上翻页期间临时寄存的原基准（BeginPrependBatch 存、EndPrependBatch 还原）。
    // 翻页批次会被整体挪到列表最前面，判定基准必须临时清零，收尾后再还给实时消息用。
    long long m_prependSavedLastMs;

    int  m_scrollY;        // 当前纵向滚动偏移

    // 滚轮的平滑滚动状态：目标位置与动画进度。滚轮只改它的目标，m_scrollY 由逐帧驱动
    // 按它的曲线推进。直接改滚动位置的操作（拖滚动条、滚到底、定位消息、清空）会取消它。
    ChatSmoothScroll m_smooth;

    // 与逐帧驱动共用的主人令牌，见 ChatScrollAnimToken。构造时建立，析构时把主人置空。
    std::shared_ptr<ChatScrollAnimToken> m_scrollAnimToken;

    // 逐帧驱动是否已挂在 DuiAnimMgr 上。由 EnsureScrollTicker 置位、OnScrollTickerDone 复位。
    bool m_scrollTickerRunning;

    int  m_contentHeight;  // 全部消息行的总高
    int  m_viewHeight;     // 视口高（消息行区域的高，已扣除多选操作条）
    bool m_multiSelect;    // 是否处于多选模式
    // 是否显示已读回执：单聊窗开启，群聊窗保持关闭。AppendMessage 新建的每一行都继承它。
    bool m_readReceiptEnabled;
    // 对方的已读位点（服务器 msgid）：自己发出的消息 msgid 不大于它即为对方已读。
    // 只增不减；0 表示还不知道。由 ApplyPeerReadPosition 更新，AppendMessage 与
    // BackfillSentMessage 据它为新出现的行当场判定。
    long long m_peerReadMsgid;

    // 头像要画成灰度的发送者（别人，即当前离线的人）的 userid 集合。由 SetSenderAvatarGray
    // 维护，AppendMessage 据它为新追加的对方气泡设置灰彩。
    std::set<int64_t> m_graySenders;
    // 本人的气泡头像是否画成灰度（本端掉线或正在连接时为 true）。由 SetSelfAvatarGray 维护，默认 false。
    bool m_selfAvatarGray;

    // "该成员是否仍在群里"的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 IsMemberInGroup 恒返回 true。
    MemberInGroupQuery m_memberQuery;
    void*              m_memberQueryUd;

    // "查成员在本群的角色"的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 MemberNetRole 恒返回 -1。
    MemberRoleQuery m_roleQuery;
    void*           m_roleQueryUd;

    // "该联系人是否被特别关注"的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 IsStarred 恒返回 false。
    StarredQuery m_starredQuery;
    void*        m_starredQueryUd;

    // 「本人是否在本群关注了 TA 的发言」的查询钩子与其透传数据（借用，不持有）。群聊窗装上，
    // 单聊窗不装 —— 为空时 IsGroupFollowed 恒返回 false。
    StarredQuery m_groupFollowQuery;
    void*        m_groupFollowQueryUd;

    // 本人在本群的角色，取值见 grouprole::NetGroupRole；-1 表示未知（单聊窗 / 尚未拉到群资料）
    int m_myNetRole;

    // 文字消息右键菜单里「添加到备忘」的出现方式，取值见 ChatMemoMenuMode；默认不出现，由宿主聊天窗设置
    int m_memoMenuMode;

    // 是否处于密聊限制（单聊窗处于密聊中或只能查看）：为 true 时整张列表没有右键菜单、不响应 Ctrl+C、不能多选、
    // 气泡不能拖出。由宿主聊天窗切换状态时设置（docs/密聊方案.md 4.5）
    bool m_secretRestricted;

    // 滚动条滑块拖动状态。
    bool m_thumbDragging;    // 是否正在用鼠标拖动滑块
    int  m_thumbDragOffsetY; // 拖动开始时鼠标相对滑块顶部的 y 偏移（保持抓点不跳）
    CChatScrollFade m_scrollFade;   // 滑块的淡入淡出（2026-10-04 起，与 balloonui 滚动条同一时序），随本控件销毁

    // ---- 离屏合成缓冲 ----
    // 气泡用 GDI+ 自绘，GDI 裁剪区约束不住 GDI+，滚动时气泡会溢出消息
    // 区。改为把气泡画进这张离屏位图（位图物理边界兜住一切绘制），再
    // 只把消息行可视区那一条 BitBlt 回真 DC。
    HDC     m_memDC;       // 离屏合成 DC
    HBITMAP m_memBmp;      // 离屏合成位图
    HBITMAP m_memOldBmp;   // m_memDC 的原始位图（释放前须换回）
    int     m_memW;        // 离屏位图当前宽
    int     m_memH;        // 离屏位图当前高
};

} // namespace balloonchatwnd
