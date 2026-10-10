/**
 *  MemoEditDialog.h
 *
 *  备忘编辑对话框：新建、修改或查看一条备忘（个人备忘与群备忘共用，群备忘多「提醒对象」一行）。
 *  由备忘中枢持有、同一时刻只开一个；是 owner（备忘窗口或群聊窗）的模态窗口：打开期间 owner 被禁用，
 *  关闭后恢复（做法与 CRelayComposeDlg 相同，判定复用 ClosePromptHostGuard）。点「保存」后经中枢发 2402，
 *  应答到达时中枢调 OnSaveResult：成功则关闭，失败则在对话框里写出原因并保持打开，已填写的内容不丢。
 *  只用 balloonui 现成控件（DuiEdit / DuiSpinBox / DuiComboBox / DuiButton / DuiLabel），不改 balloonui。
 *
 *  选择提醒对象复用选人对话框 CCreateGroupDlg 的 kPurposeMemoTargets 用途，本类作为它的 IMemberPickSink。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../net/MemoProtocolClient.h"
#include "../../net/ContactsProtocolClient.h"         // net::ContactItem（候选提醒对象）
#include "../BalloonMain/ClosePromptDecision.h"       // ClosePromptHostGuard（模态时禁用 / 恢复 owner）
#include "../BalloonMain/CreateGroupDlg.h"            // CCreateGroupDlg / IMemberPickSink（选择提醒对象）
#include "MemoCenter.h"                               // MemoEditMode

namespace balloonwjui { class DuiControl; class DuiLabel; class DuiEdit; class DuiButton; class DuiSpinBox; class DuiComboBox; }
class CMemoFullscreenWindow;

/**
 *  备忘编辑对话框。
 */
class CMemoEditDialog : public balloonwjui::DuiFrameWindow, public IMemberPickSink
{
public:
    CMemoEditDialog();
    ~CMemoEditDialog();

    /**
     *  打开对话框；已开着时先关掉再按这一次的内容重开。
     *    owner：  发起打开的窗口，对话框居中摆在它上面并在打开期间禁用它；句柄无效时不模态；
     *    item：   要编辑的备忘；
     *    mode：   新建、修改或只读，取值见 balloonmemo::MemoEditMode；
     *    members：群备忘的候选提醒对象（本群成员）；个人备忘传空。
     *  @return 窗口已显示返回 true
     */
    bool Open(HWND owner, const net::MemoItem& item, balloonmemo::MemoEditMode mode,
              const std::vector<net::ContactItem>& members);

    /**
     *  保存应答（中枢转来）。对话框不在等应答时不做任何事。
     *    code：0 成功；其余取值见 Protocol.h 的 error_code。
     */
    void OnSaveResult(int code);

    /**
     *  正在编辑的备忘被删除了（推送或别处删除）：编辑已无意义，关闭对话框。
     *    memoid：被删除的备忘 ID。
     */
    void OnMemoDeleted(int64_t memoid);

    /**
     *  关闭对话框（注销时中枢调用）。
     */
    void CloseDialog();

    // IMemberPickSink：选人对话框确认了提醒对象
    void OnMembersPicked(const std::vector<int64_t>& userIds) override;

    BEGIN_MSG_MAP(CMemoEditDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //销毁时兜底恢复 owner，只旁听、不消费
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    bool BuildWindow(HWND owner, bool groupMode);
    void CaptureControls();

    //把 m_item 填进各控件
    void FillControls();

    //按「重复」的当前选项决定「每周」一行显示与否、「结束」一行可不可用
    void SyncRepeatDependents();

    //按「提醒对象」的当前选项决定「选择成员」可不可用，并刷新已选几人
    void SyncTargetDependents();

    //从各控件读回备忘（标题、备注之外的字段），标题与备注另读
    net::MemoItem ReadControls() const;

    void OnSubmit();
    void OpenTargetPicker();

    //「预览效果」：按当前填写的标题、时间、备注，在本对话框所在的显示器上播放全屏提醒（2026-10-04 起）。
    //用本对话框自己的全屏窗口实例，不经过备忘中枢，不影响真实提醒
    void ShowFullscreenPreview();

    //收起正在播放的预览（对话框关闭或销毁时调用）
    void ClosePreview();
    void ShowError(const std::wstring& text);
    void SetSaving(bool saving);
    void DisableOwner();
    void RestoreOwner();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    net::MemoItem              m_item;          //打开时的备忘；保存时在它的基础上改
    balloonmemo::MemoEditMode  m_mode;          //打开方式
    bool                       m_groupMode;     //是不是群备忘
    std::vector<net::ContactItem> m_members;    //候选提醒对象（群备忘），打开时复制一份
    std::vector<int64_t>       m_targets;       //当前选中的提醒对象（「指定成员」时有效）
    bool                       m_saving;        //是否已发出保存、正在等应答
    HWND                       m_ownerHwnd;     //owner 的句柄，打开期间它被禁用
    balloonmain::ClosePromptHostGuard m_ownerGuard;   //是否由本窗口禁用了 owner、关闭时要不要恢复
    std::unique_ptr<CCreateGroupDlg> m_picker;  //选择提醒对象的选人对话框，首次使用时创建
    std::unique_ptr<CMemoFullscreenWindow> m_previewWindow;   //「预览效果」用的全屏提醒窗口，首次预览时创建，随本对话框析构

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiEdit*     m_pTitle;     //标题
    balloonwjui::DuiSpinBox*  m_pYear;      //开始时间：年
    balloonwjui::DuiSpinBox*  m_pMonth;     //开始时间：月
    balloonwjui::DuiSpinBox*  m_pDay;       //开始时间：日
    balloonwjui::DuiSpinBox*  m_pHour;      //开始时间：时
    balloonwjui::DuiSpinBox*  m_pMinute;    //开始时间：分
    balloonwjui::DuiComboBox* m_pAdvance;   //提醒（提前量）
    balloonwjui::DuiComboBox* m_pRepeat;    //重复
    balloonwjui::DuiButton*   m_pWeekdays[memo::kDaysPerWeek];   //星期一到星期日的勾选框
    balloonwjui::DuiButton*   m_pEndNever;  //结束：永不结束
    balloonwjui::DuiButton*   m_pEndCount;  //结束：提醒 N 次
    balloonwjui::DuiSpinBox*  m_pCount;     //N
    balloonwjui::DuiButton*   m_pStylePopup;//方式：右下角弹窗
    balloonwjui::DuiButton*   m_pStyleFull; //方式：全屏动画
    balloonwjui::DuiButton*   m_pTargetAll; //提醒对象：全体成员（仅群模式）
    balloonwjui::DuiButton*   m_pTargetPick;//提醒对象：指定成员（仅群模式）
    balloonwjui::DuiButton*   m_pPickBtn;   //「选择成员」（仅群模式）
    balloonwjui::DuiLabel*    m_pTargetText;//已选几人（仅群模式）
    balloonwjui::DuiEdit*     m_pNote;      //备注
    balloonwjui::DuiLabel*    m_pError;     //提示行
    balloonwjui::DuiButton*   m_pCancel;    //「取消」/「关闭」
    balloonwjui::DuiButton*   m_pSave;      //「保存」
    balloonwjui::DuiButton*   m_pDelete;    //左下角「删除」：只在修改已有的一条时显示
    balloonwjui::DuiControl*  m_pWeekdayRow;//「每周」那一行：其中的控件只在每周重复时显示
    balloonwjui::DuiControl*  m_pButtonRow; //底部按钮行：「删除」显隐之后对它强制重排

    HBITMAP m_hTitleIcon;   //标题栏左侧图标位图；SetIcon 只借用，本类持有、析构时释放
};
