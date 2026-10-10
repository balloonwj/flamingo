/**
 *  RelayDetailModel.h
 *
 *  接龙详情窗（RelayDetailWindow）的显示模型：根据卡片内容、发起人、当前用户的群角色与禁言状态，
 *  算出详情窗要显示的标题、说明、状态行、完整名单，以及底部输入区里各按钮的显隐与文字；另外负责
 *  提交前的文本检查与提示文字。纯逻辑，不碰窗口与绘制，可被 FlamingoUnit 直接测试。
 *  交互设计见 docs/群接龙方案.md §3.3。
 *
 *  典型用法（详情窗打开、收到推送、操作失败之后重画时）：
 *      ballooncard::RelayDetailView view;
 *      ballooncard::BuildRelayDetailView(card, creatorId, selfId, selfRole, selfMuted, nowMs, names, view);
 *      // 按 view 的各字段设置控件文字与显隐
 *
 *  本头文件使用 CString，包含它之前须已引入 ATL 的 atlstr.h（主工程经预编译头，单测经
 *  Tests\stdafx.h，实现文件见 RelayDetailModel.cpp 开头）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <windows.h>   // HWND

#include "../../net/CardProtocolClient.h"           // net::RelayCardContent
#include "../BalloonChatWnd/ChatRelayCard.h"        // IRelayNameResolver、CanCloseRelay 等

namespace ballooncard {

/**
 *  接龙详情窗客户区控件的数字 id，窗口内唯一。放在这里而不是 RelayDetailWindow.cpp 里，
 *  是为了让单元测试（RDW1）能直接检查取值。
 */
//编号从 101 起：DuiFrameWindow 标题栏的最小化 / 最大化 / 关闭按钮固定使用 1 / 2 / 3（见 balloonui
//DuiFrameWindow.cpp 的 OnDuiChildNotify），它们的点击通知与客户区控件走同一条 WM_DUI_NOTIFY。
//客户区控件与之同号时，窗口自己的通知处理会先把标题栏按钮的点击当成该控件处理：2026-10-03
//之前详情窗「撤回我的」是 3，点标题栏的 X 关不掉窗口，已参与时还会弹出撤回确认。
enum RelayDetailCtrlId
{
    kRelayDetailIdInput      = 101,   // 本人那一条的输入框（DuiEdit，单行）
    kRelayDetailIdSubmit     = 102,   // 「提交 / 修改」按钮
    kRelayDetailIdWithdraw   = 103,   // 「撤回我的」按钮
    kRelayDetailIdCloseRelay = 104,   // 「结束接龙」按钮
    kRelayDetailIdCopy       = 105,   // 「复制为文本」按钮
};

/**
 *  打开一张接龙卡片详情窗的请求。群聊窗收到 WM_CHAT_RELAY_ACTION 后在堆上构造，作为
 *  FMG_MSG_OPEN_RELAY_DETAIL 的 lParam 投给主面板，主面板处理后 delete。
 *  点的是系统提示行、而卡片不在群聊窗当前视图里时，群聊窗拿不到卡片内容与发起人，
 *  m_contentJson 留空、m_creatorId 为 0，由主面板从本地库补齐。
 */
struct RelayDetailRequest
{
    int         m_ctype;        //卡片所在会话的类型，见 enum conversation_type；群聊为 2
    int64_t     m_cid;          //会话ID：群聊为群 id
    int64_t     m_msgid;        //卡片消息的服务器 msgid（恒 >0）
    int64_t     m_creatorId;    //卡片发起人（卡片消息的发送方）userid；0 表示群聊窗不知道
    int         m_selfRole;     //当前用户在群里的角色，见 group_member_role；决定能否结束接龙
    std::string m_contentJson;  //卡片当前内容（紧凑 JSON，UTF-8）；空表示群聊窗不知道
    bool        m_focusInput;   //打开后是否把焦点放到本人那一条的输入框（点了卡片上的「参与接龙」「修改我的」）
    HWND        m_ownerHwnd;    //发起打开的群聊窗句柄：详情窗以它为 owner，打开期间它被禁用；NULL 表示不模态

    RelayDetailRequest()
        : m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
        , m_creatorId(0)
        , m_selfRole(0)
        , m_focusInput(false)
        , m_ownerHwnd(NULL)
    {
    }
};

/**
 *  详情窗名单里的一行。
 */
struct RelayDetailEntry
{
    CString m_text;     //「序号. 名字　内容」，序号从 1 起
    bool    m_isSelf;   //是否当前用户自己那一条；详情窗把它画成主题色，便于找到

    RelayDetailEntry()
        : m_isSelf(false)
    {
    }
};

/**
 *  详情窗要显示的全部内容，由 BuildRelayDetailView 生成。
 */
struct RelayDetailView
{
    CString                       m_title;          //标题；卡片内容里标题为空时为「接龙」
    CString                       m_note;           //说明全文，可多行，可以为空
    CString                       m_statusText;     //状态行，写法见 BuildRelayDetailView 的说明
    std::vector<RelayDetailEntry> m_entries;        //完整名单，按名次排列
    int                           m_state;          //按本机时间换算后的接龙状态，取值见 enum relay_state

    bool                          m_showInput;      //是否显示底部输入区（输入框与提交按钮）：只在进行中显示
    bool                          m_inputEnabled;   //输入框与提交按钮是否可用：被禁言、名额已满且本人未参与时不可用
    CString                       m_inputHint;      //输入区上方的说明文字，如「你已被禁言，不能参与接龙」；为空时不显示
    CString                       m_submitLabel;    //提交按钮的文字：本人未参与时为「提交」，已参与时为「修改」
    bool                          m_showWithdraw;   //是否显示「撤回我的」：进行中、本人已参与且未被禁言
    bool                          m_showClose;      //是否显示「结束接龙」：进行中，且当前用户是发起人、群主或管理员
    bool                          m_joined;         //当前用户是否已在名单里
    std::string                   m_selfText;       //当前用户那一条的内容（UTF-8）；未参与时为空。打开详情窗时预填进输入框

    RelayDetailView()
        : m_state(0)
        , m_showInput(false)
        , m_inputEnabled(false)
        , m_showWithdraw(false)
        , m_showClose(false)
        , m_joined(false)
    {
    }
};

/**
 *  生成详情窗要显示的全部内容。
 *  状态行：进行中为「共 N 人参与」，有截止时间时追加「 · 截止 10-03 18:00」，名额已满且本人未参与时
 *  追加「 · 已满」；被提前结束时为「共 N 人参与 · 李四 于 10-02 16:00 结束了接龙」（结束人或结束时间
 *  缺失时相应省略，两样都缺时写「 · 已结束」）；已截止时为「共 N 人参与 · 已于 10-03 18:00 截止」。
 *  @param card      卡片内容
 *  @param creatorId 卡片发起人（卡片消息的发送方）userid
 *  @param selfId    当前登录用户 userid
 *  @param selfRole  当前用户在群里的角色，取值见 group_member_role
 *  @param selfMuted 当前用户是否被禁言（详情窗收到 306 拒绝之后置为 true）
 *  @param nowMs     本机当前时间，Unix 毫秒
 *  @param names     名字解析接口；调用期间须有效，本函数不保存它
 *  @param out       [出参] 详情窗要显示的内容
 */
void BuildRelayDetailView(const net::RelayCardContent& card, int64_t creatorId, int64_t selfId, int selfRole,
                          bool selfMuted, int64_t nowMs, const balloonchatwnd::IRelayNameResolver& names,
                          RelayDetailView& out);

//提交前文本检查的结果，由 CheckRelaySubmit 给出
enum RelaySubmitCheck
{
    kRelaySubmitOk        = 0,   //可以提交：已去掉首尾空白，内容与本人现有那条不同（或本人还没参与）
    kRelaySubmitEmpty     = 1,   //去掉首尾空白后为空
    kRelaySubmitMultiline = 2,   //中间含换行（接龙每人一行，服务端会以 3 拒绝）
    kRelaySubmitTooLong   = 3,   //超过 kRelayTextMaxChars 个字符
    kRelaySubmitUnchanged = 4,   //与本人现有那条完全相同：不必发请求（服务端对重复提交也只回成功、不推送）
};

/**
 *  提交前检查输入框里的文字。规范化规则与服务端相同（net::CardProtocolClient::normalizeRelayText）。
 *  @param rawUtf8    输入框里的文字（UTF-8）
 *  @param joined     当前用户是否已在名单里
 *  @param selfText   当前用户现有那一条的内容（UTF-8）；joined 为 false 时忽略
 *  @param normalized [出参] 去掉首尾空白后的文字；只在返回 kRelaySubmitOk 时写入
 *  @return 取值见 RelaySubmitCheck
 */
int CheckRelaySubmit(const std::string& rawUtf8, bool joined, const std::string& selfText, std::string& normalized);

/**
 *  提交前检查不通过时，显示在输入区下方的提示文字。
 *  @param check CheckRelaySubmit 的返回值
 *  @return 提示文字；kRelaySubmitOk 返回空串
 */
CString RelaySubmitCheckText(int check);

/**
 *  结束接龙前的确认文字，如「确定结束接龙「周五团建报名」吗？结束后不能再参与、修改或撤回。」。
 *  @param card 卡片内容；标题为空时写成「确定结束这个接龙吗？……」
 *  @return 确认框正文
 */
CString BuildRelayCloseConfirmText(const net::RelayCardContent& card);

} // namespace ballooncard
