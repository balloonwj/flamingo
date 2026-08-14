/**
 *  BalloonGroupChatDlg.h
 *
 *  Group chat dialog, balloonui-based replacement for the legacy
 *  CGroupChatDlg in GroupChatDlg.h.
 *
 *  P0 scope: skeleton -- public surface (members + business methods)
 *  matches what callers (CMainDlg, ChatTabMgr) need; all business
 *  methods are empty stubs; the dialog opens as an empty DuiFrameWindow.
 *
 *  balloonwj@qq.com   2026.05.20
 */

#pragma once

#include <vector>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"     // WM_DUI_NOTIFY / struct DuiNotify

class CFlamingoClient;
class CFaceList;
class CCascadeWinManager;
class CBuddyMessage;        // 接收消息处理用，前置声明，真定义在 UserSessionData.h

// balloonui 控件 —— 仅作成员指针类型用，前置声明即可，真定义在 .cpp 引入。
namespace balloonwjui { class DuiRichEditHost; class DuiButton; class DuiTreeView; }

/**
 *  CBalloonGroupChatDlg
 *
 *  Drop-in replacement for the legacy CGroupChatDlg. Public surface
 *  mirrors the legacy class so call-site code does not need to change
 *  beyond the #include line.
 */
class CBalloonGroupChatDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonGroupChatDlg();
    ~CBalloonGroupChatDlg();

    // ---- Business notification entry points (called by MainDlg) ----
    // P0: empty stubs. P1+ will wire these to UI updates.

    /// Called when a group chat message arrives.
    ///   nGroupCode: group id (must match m_nGroupCode)
    ///   nMsgId:     message id assigned by the receive pipeline
    void OnRecvMsg(UINT nGroupCode, UINT nMsgId);

    /// Group's metadata changed (name, announcement, ...).
    void OnUpdateGroupInfo();

    /// Group's member count or number label changed.
    void OnUpdateGroupNumber();

    /// Group's avatar changed.
    void OnUpdateGroupHeadPic();

    /// A group member's contact number changed.
    ///   wParam / lParam: legacy parameter passthrough -- the upstream
    ///   FMG_MSG_UPDATE_GMEMBER_NUMBER message packs member id and
    ///   related data here. Stub ignores them.
    void OnUpdateGMemberNumber(WPARAM wParam, LPARAM lParam);

    /// A group member's avatar changed. Parameters identical to above.
    void OnUpdateGMemberHeadPic(WPARAM wParam, LPARAM lParam);

    // ---- P1a: balloonui 客户区 UI ----

    /**
     *  构建并装载群聊客户区控件树（来自 BalloonChatLayout 的内嵌 XML），
     *  并把关键控件指针缓存到 m_pXxx 字段。
     *
     *  必须在窗口 Create 之后调用 —— SetClientContent 与
     *  DuiRichEditHost::EnsureCreated 都依赖 host HWND 已就绪。
     *  每次调用都重建客户区（SetClientContent 替换旧树），因此窗口被
     *  销毁后重新 Create 时应再调一次。
     */
    void InitChatUi();

    // WM_DUI_NOTIFY 路由：拦底部"关闭 / 发送"按钮的点击；其余通知
    // （标题栏 min/max/close 等）通过 CHAIN 交回 DuiFrameWindow。
    BEGIN_MSG_MAP(CBalloonGroupChatDlg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

    /**
     *  WM_DUI_NOTIFY 处理函数。
     *    lParam：指向调用栈上的 balloonwjui::DuiNotify。
     *  命中底部按钮则消费消息；否则置 bHandled=FALSE 让基类继续处理。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- P1b: 纯文本消息收发 ----

    /**
     *  采集输入区文本 → 校验 → 回显到接收区 → 发往服务器 → 清空输入区。
     *  由 OnDuiNotify 在"发送"按钮点击时调用。
     */
    void DoSendMessage();

private:
    /**
     *  把一条收到的群消息追加到接收区。P1b 只渲染纯文本部分
     *  （CONTENT_TYPE_TEXT），图片 / 表情 / 文件等留待 P2。
     *    lpMsg：消息对象，所有权属于 CMessageList，本函数只读不释放。
     */
    void AppendGroupMsgToRecv(CBuddyMessage* lpMsg);

    /**
     *  按当前群信息刷新标题栏：标题文字 = 群名称，图标 = 群头像。
     *  群信息尚未就绪时静默跳过。InitChatUi 与群信息更新通知都会调。
     */
    void RefreshTitleBar();

public:

public:
    // ---- Public fields the callers set right after `new` ----

    CFlamingoClient*    m_lpFMGClient;          // owner client (not owned)
    CFaceList*          m_lpFaceList;           // shared face library (not owned)
    CCascadeWinManager* m_lpCascadeWinManager;  // window cascade manager (not owned)
    HWND                m_hMainDlg;             // main dialog HWND (for posting back)
    UINT                m_nGroupCode;           // group id this dialog represents

    // ---- P1a: 缓存的客户区控件指针 ----
    // 由 InitChatUi 填充；指向 DuiFrameWindow 客户区控件树里的节点，
    // 所有权属于控件树，本类<u>不</u>负责释放。未 InitChatUi 时为 NULL。
    balloonwjui::DuiRichEditHost* m_pRecvEdit;       // 消息接收区（只读）
    balloonwjui::DuiRichEditHost* m_pSendEdit;       // 消息输入区（可编辑）
    balloonwjui::DuiRichEditHost* m_pGroupAnnounce;  // 右侧栏群公告（只读）
    balloonwjui::DuiTreeView*     m_pMemberList;     // 右侧栏群成员列表
    balloonwjui::DuiButton*       m_pBtnClose;       // 底部"关闭(C)"按钮
    balloonwjui::DuiButton*       m_pBtnSend;        // 底部"发送(S)"按钮

    // 标题栏头像位图：LoadChatAvatar 新建，本类持有，dtor 释放。NULL = 未设。
    HBITMAP                       m_hAvatarBmp;

    // 已注册悬停 tooltip 的工具栏按钮 —— InitChatUi 重建控件树前、以及
    // 本类析构时，须据此向 DuiToolTipMgr 取消注册，避免悬空指针。
    std::vector<balloonwjui::DuiControl*> m_tooltipCtrls;
};

// Backwards-compat alias: keeps legacy CGroupChatDlg references compiling
// while transparently routing them to the balloonui implementation.
typedef CBalloonGroupChatDlg CGroupChatDlg;
