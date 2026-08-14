/**
 *  BalloonProfileDlg.h
 *
 *  「个人信息」对话框窗口类。继承 balloonwjui::DuiFrameWindow,客户区由
 *  BalloonProfileLayout::BuildProfileBody 装入,数据由
 *  BalloonProfileLayout::ApplyProfileDataToView 注入。
 *
 *  非模态浮窗,由主面板 CBalloonMainPanel 在头像被点击时 OpenForCurrentUser
 *  调出。同一实例可多次打开 —— 已 IsWindow 时直接前置,未创建则 Create。
 *
 *  本期范围(参考 design/个人信息.png / 个人信息02.png):
 *    · 查看态:8 行信息只读展示。前 6 行(昵称/签名/性别/生日/所在地/邮箱)
 *      是本人可改的资料;末两行(部门/职位)来自企业组织架构、由管理员在
 *      admin 控制台维护,本人改不了,编辑态下也保持只读
 *    · 编辑态:前 6 行右侧切到 DuiEditHost + DuiComboBox(性别),底部按钮
 *      切到 "取消 / 保存"
 *    · 保存按钮走"乐观更新 + 失败回退":先改本地 m_UserInfo 并切回查看态,
 *      同时发 cmd_update_profile;应答经主面板 OnUpdateProfileResult 转到
 *      本类 OnSaveResult,失败时从保存前的 m_snapXxx 快照回退并弹提示
 *
 *  本文件遵循 balloonui 头文件约定:include 它之前调用方须已引入 ATL/WTL
 *  (Flamingo 侧由 stdafx.h 保证)。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#pragma once

#include "BalloonProfileLayout.h"   // ProfileVisualResources
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

class CFlamingoClient;

namespace net { struct UpdateProfileResponse; }   // OnSaveResult 入参,前置声明免引头

namespace balloonmain {

/**
 *  CBalloonProfileDlg —— 「个人信息」对话框。
 */
class CBalloonProfileDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonProfileDlg();
    ~CBalloonProfileDlg();

    /**
     *  打开 / 前置个人信息对话框,数据取自 pFMGClient->m_UserMgr.m_UserInfo。
     *    hParent     父窗口(通常是主面板 HWND);用于窗口居中参照。
     *    pFMGClient  数据源;NULL 则不显示,本类<u>不持有</u>所有权。
     *  已 IsWindow 时:从最小化还原 + SetForegroundWindow 前置;否则 Create
     *  并装填 UI。
     */
    void OpenForCurrentUser(HWND hParent, CFlamingoClient* pFMGClient);

    /**
     *  设置本人在企业组织架构里的部门与职位，用于资料窗末尾那两行只读展示。
     *
     *  这两项<u>不在</u> m_UserMgr.m_UserInfo 里 —— 它们来自 cmd_get_contacts 下发的
     *  部门树，只有主面板持有那份缓存，故由主面板在打开本窗口前灌进来。存成成员
     *  是因为 1108 资料变更推送会再走一次 RefreshFromUserInfo，那时若不留底这两行
     *  会被刷成空。
     *
     *    dept      部门路径，形如"产品部 / 设计组"；无部门归属时传空串。
     *    position  职位；未设置时传空串。
     *  两者为空时对应行显示"—"。本方法可在窗口创建前后任意时刻调用。
     */
    void SetOrgInfo(const CString& dept, const CString& position);

    /**
     *  是否有一次"保存资料"正在等待服务端应答。主面板 OnUpdateProfileResult 据此
     *  决定把 cmd_update_profile 应答交给本对话框处理(而非签名编辑路径)。
     */
    bool HasPendingSave() const { return m_savePending; }

    /**
     *  处理"保存资料"的服务端应答(由主面板 OnUpdateProfileResult 转交)。
     *  成功:乐观更新已生效,清除 pending;失败:从快照回退 m_UserInfo、刷新查看态、
     *  弹错误提示(窗口已关时只回退数据、跳过 UI)。
     *    resp  cmd_update_profile 通用 {code,msg} 应答;code==0 为成功。
     */
    void OnSaveResult(const net::UpdateProfileResponse& resp);

    // WM_DUI_NOTIFY 自处理,其他消息经 CHAIN 交回 DuiFrameWindow。
    BEGIN_MSG_MAP(CBalloonProfileDlg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    /**
     *  WM_DUI_NOTIFY 处理:4 按钮点击 + 4 状态胶囊点击。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  在查看态 / 编辑态之间切换:翻转 View 与 Edit 系列控件的可见性,
     *  以及底部 Close/Edit ↔ Cancel/Save 的可见性,最后强制重排重绘。
     *    edit  true 切到编辑态;false 切到查看态。
     */
    void SwitchEditMode(bool edit);

    /**
     *  从 m_lpFMGClient->m_UserMgr.m_UserInfo 重新构造 ProfileDataInput
     *  并调 ApplyProfileDataToView 注入所有控件,然后按 m_nStatus 高亮
     *  对应状态胶囊。OpenForCurrentUser 与 Cancel 按钮路径都用它。
     */
    void RefreshFromUserInfo();

    /**
     *  「保存」按钮逻辑:离线拦截 → 读 6 个编辑控件当前值 → 生日格式校验(非法弹提示
     *  并留在编辑态) → 存旧值快照 → 乐观写入 m_UserInfo → 刷新查看态并切回 → 编码
     *  UTF-8 发 cmd_update_profile。应答经主面板回投到 OnSaveResult。
     */
    void DoSaveProfile();

    /**
     *  依据 m_lpFMGClient->m_UserMgr.m_UserInfo 的昵称(为空时取账号)把标题栏
     *  文字刷成 "用户名 - 个人信息"。首次 Create 与每次重新打开都调,保证昵称
     *  被改后标题同步;m_lpFMGClient 为 NULL 时不动标题。
     */
    void UpdateTitleFromUserInfo();

private:
    /**
     *  Lazy 创建本对话框需要的 GDI 位图资源(编辑资料 / 保存按钮的圆角橙底,三态)。
     *  已创建则直接返回,幂等。失败时单张为 NULL,helper 内部相应跳过那一路视觉,不会崩。
     */
    void EnsureVisualResources();

    CFlamingoClient* m_lpFMGClient;       // 数据源,引用不持有
    CString          m_dept;              // 本人所在部门路径(来自主面板的通讯录缓存),只读展示
    CString          m_position;          // 本人职位(同上),只读展示
    HWND             m_hOwnerWnd;         // 主面板 HWND(OpenForCurrentUser 的 hParent);乐观保存后 PostMessage 它即时刷新 Header
    bool             m_editMode;          // 当前模式;false=查看,true=编辑
    HBITMAP          m_titleIconBmp;      // 标题栏程序图标位图(IDR_MAINFRAME 合成到标题栏底色),本类持有,析构 DeleteObject
    HICON            m_hTaskIconBig;      // 任务栏 / Alt-Tab 大图标(IDR_MAINFRAME,ICON_BIG),本类持有,析构 DestroyIcon
    HICON            m_hTaskIconSmall;    // 任务栏 / 标题栏小图标(IDR_MAINFRAME,ICON_SMALL),本类持有,析构 DestroyIcon
    ProfileVisualResources m_resources;   // 视觉精修位图集合,dlg 持有,析构释放

    // ---- 保存资料的未决态与失败回退快照 ----
    bool    m_savePending;     // true=有一次保存正在等服务端应答(OnSaveResult 收到后清)
    CString m_snapNickname;    // 保存前昵称快照(服务端拒绝时回退 m_UserInfo 用)
    CString m_snapSignature;   // 保存前签名快照
    int     m_snapGender;      // 保存前性别快照(0 男 / 1 女)
    long    m_snapBirthday;    // 保存前生日快照(YYYYMMDD)
    CString m_snapAddress;     // 保存前所在地快照
    CString m_snapEmail;       // 保存前邮箱快照
};

} // namespace balloonmain
