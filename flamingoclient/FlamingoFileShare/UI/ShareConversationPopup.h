/**
 *  本会话共享列表浮层，ShareConversationPopup.h
 *
 *  docs/文件共享方案.md 7.4「本会话共享列表」：聊天窗工具栏的「文件共享」按钮弹出的浮层，列出本会话里本人能看到的共享
 *  （本人发起的与共享给本人的两份 2205 应答合并），底部一个「发起共享」按钮。每一行左边是本机的系统类型图标（磁盘、
 *  文件夹，或按扩展名取的文件类型图标，2026-10-03 起），右边是名称、「某某的共享」与状态，行尾按本人身份给出操作：
 *  共享给本人的可以「打开」；本人发起的可以「调整成员」「取消共享」；群主与群管理员还可以取消群里别人的共享（本人的
 *  群角色取自宿主回送的会话成员）。双击一行等同于点它的「打开」（没有「打开」的行双击不做任何事）。取消前弹一次确认，
 *  取消后重新拉取列表。
 *
 *  窗口形态：无边框的顶层浮窗（WS_POPUP + CS_DROPSHADOW），用 balloonui 的 DuiHost 接管；贴着工具栏按钮弹出，靠近屏幕
 *  边缘时翻到按钮上方并限制在工作区内（UI 约定第 3 条，几何运算用 DuiMenuPlacement.h）。失去激活时关闭（弹确认框期间除外）。
 *  建窗时不设所有者：DuiHost 用 ::GetParent 找通知的投递目标，带所有者的 WS_POPUP 会把控件通知投给所有者（聊天窗）。
 *  在通知处理函数里关闭窗口一律投递 WM_CLOSE，不在处理函数里直接销毁正在派发消息的控件树。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（CFileShareModule::OpenConversationShares）：
 *      m_convPopup.Open(owner, &m_service, m_host, this, ctype, cid, anchor);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../FileShareApi.h"
#include "../Engine/ShareService.h"   //IShareListObserver / IShareManageObserver
#include "ShareUiHost.h"

namespace balloonwjui {
class DuiLabel;
class DuiButton;
class DuiVBox;
class DuiScrollView;
}

namespace fileshare {

/**
 *  CShareConversationPopup —— 本会话共享列表浮层。只在 UI 线程使用。
 */
class CShareConversationPopup : public balloonwjui::DuiHost, public IShareListObserver, public IShareManageObserver
{
public:
    CShareConversationPopup();
    ~CShareConversationPopup();

    /**
     *  弹出浮层并拉取本会话的共享；已弹出时先关掉再按新的会话弹出。
     *  @param owner   聊天窗（打开其它窗口时作所有者），可为 NULL
     *  @param service 业务总控（不拥有），不可为 NULL
     *  @param host    宿主（取显示名、会话成员；不拥有），不可为 NULL
     *  @param ui      打开其它窗口的入口（模块，不拥有），不可为 NULL
     *  @param ctype   会话类型
     *  @param cid     会话 id（本人视角）
     *  @param anchor  按钮的屏幕矩形；为 NULL 时贴着光标
     *  @return 已弹出返回 true
     */
    bool Open(HWND owner, CShareService* service, IFileShareHost* host, IShareUiHost* ui, int ctype, int64_t cid,
              const RECT* anchor);

    //关闭浮层：不再接收列表与取消的结果、销毁 HWND。未弹出时调用是安全的
    void Close();

    /**
     *  宿主回送了会话成员：从中取本人的群角色（决定能不能取消别人的共享）。会话不符或未弹出时忽略。
     */
    void OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members, uint32_t count);

    //IShareListObserver：一份列表到达
    virtual void OnShareListResult(uint32_t requestId, int scope, int code, const std::vector<ShareListEntry>& shares) override;

    //IShareManageObserver：取消共享的结果
    virtual void OnShareManageResult(const ShareManageResult& result) override;

    /**
     *  对一行执行一项操作（按钮点击时调用，测试直接调用）。
     *  @param row       行号
     *  @param action    share_row_action 之一
     *  @param confirmed 取消共享时为真表示不再弹确认框（视为已确认）；按钮传假
     *  @return 已执行（或已发出取消请求）返回 true；行号或操作非法、用户在确认框里放弃返回 false
     */
    bool DoRowAction(size_t row, int action, bool confirmed);

    //「发起共享」：请模块打开发起共享对话框，并关闭浮层
    void OnCreateClicked();

    /**
     *  「管理全部共享」（2026-10-03 起，按钮点击时调用，测试直接调用）：请模块打开「文件共享」窗口并记住本会话（之后在那里点
     *  「发起共享」直接在本会话里发起），然后关闭浮层。
     *  @return 「文件共享」窗口已显示返回 true
     */
    bool OnManageAllClicked();

    /**
     *  双击了一行：该行能「打开」时打开它（等同于点「打开」），否则不做任何事。
     *  @param row 行号
     *  @return 已打开返回 true
     */
    bool OnRowDoubleClick(size_t row);

    /**
     *  按客户区坐标找行：只认列表视口里看得见的行，落在行内按钮上也算这一行。
     *  @param pt 客户区坐标
     *  @return 行号；不在任何行上返回 -1
     */
    int RowAtPoint(POINT pt) const;

    //以下供测试使用
    size_t GetRowCount() const;
    RECT GetRowRect(size_t row) const;      //一行的矩形（客户区坐标）；行号越界返回空矩形
    bool RowHasIcon(size_t row) const;      //这一行的类型图标是否已取到
    const ShareListEntry& GetRow(size_t row) const;
    int GetRowActions(size_t row) const;
    std::wstring GetStatusText() const;
    int GetMyRole() const;

    BEGIN_MSG_MAP(CShareConversationPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivateMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnDoubleClickMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    //WM_DUI_NOTIFY：行内按钮与「发起共享」的点击
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_ACTIVATE：失去激活时关闭（弹确认框期间除外）
    LRESULT OnActivateMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_CLOSE：关闭浮层
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_KEYDOWN：Esc 关闭
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_LBUTTONDBLCLK：双击一行打开它；落在行内按钮上时交给 DuiHost 照常处理
    LRESULT OnDoubleClickMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //释放各行类型图标的位图（控件树里引用它们的头像控件必须已经销毁）
    void ReleaseIconBitmaps(std::vector<HBITMAP>& bitmaps);

    //建窗（窗口类首次使用时注册）；成功返回 true
    bool CreatePopupWindow();

    //构建控件树
    void BuildContent();

    //按 anchor 计算浮层左上角并移动窗口
    void PlaceNear(const RECT* anchor);

    //发两份 2205（我的、共享给我的，都按本会话筛选）
    void RequestLists();

    //按 m_rows 重建列表行
    void RebuildRows();

    //设置底部提示行；isError 为真时用红字
    void SetStatus(const std::wstring& text, bool isError);

    //投递一次 WM_CLOSE（重复调用只投一次）
    void PostClose();

private:
    CShareService*              m_service;      //业务总控（不拥有）；弹出期间有效
    IFileShareHost*             m_host;         //宿主（不拥有）
    IShareUiHost*               m_ui;           //打开其它窗口的入口（不拥有）
    HWND                        m_owner;        //聊天窗；打开其它窗口时作所有者
    int                         m_ctype;        //会话类型
    int64_t                     m_cid;          //会话 id
    int                         m_myRole;       //本人的群角色；单聊或不知道时为 kShareRoleUnknown
    uint32_t                    m_mineSeq;      //「我的共享」请求编号；已有结果时为 0
    uint32_t                    m_sharedSeq;    //「共享给我的」请求编号；已有结果时为 0
    std::vector<ShareListEntry> m_mine;         //「我的共享」的结果
    std::vector<ShareListEntry> m_sharedMe;     //「共享给我的」的结果
    int                         m_listError;    //两份列表里出现的错误码；都成功时为 0
    std::vector<ShareListEntry> m_rows;         //合并后显示的各行
    std::vector<HBITMAP>        m_iconBitmaps;  //各行类型图标的位图，下标与 m_rows 对应，取不到时为 NULL；本对象拥有，
                                                //行重建或窗口关闭、引用它们的控件销毁之后释放
    std::vector<balloonwjui::DuiControl*> m_rowLines;  //各行的容器（归控件树所有），用来按坐标找行
    uint32_t                    m_cancelId;     //正在等待结果的取消请求编号；没有时为 0
    bool                        m_modal;        //是否正在显示确认框（期间失去激活不关闭）
    bool                        m_closePosted;  //是否已投递 WM_CLOSE 尚未处理

    //以下控件归窗口的控件树所有，窗口创建后有效、销毁后失效
    balloonwjui::DuiLabel*      m_title;        //标题「本会话的共享」
    balloonwjui::DuiScrollView* m_scroll;       //列表视口
    balloonwjui::DuiLabel*      m_status;       //提示行（加载中、空列表、错误）
};

} // namespace fileshare
