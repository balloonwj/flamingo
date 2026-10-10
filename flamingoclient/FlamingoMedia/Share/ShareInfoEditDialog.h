/**
 *  屏幕共享的「修改标题与描述」小窗（第 15 期），ShareInfoEditDialog.h
 *
 *  发起人在控制条「更多」菜单里点「修改标题与描述」后打开：顶部一行说明，下面是「标题」「描述」两个输入框（填好当前的
 *  标题与描述），再下面是「取消」「保存」。「保存」或在输入框里按回车时按 NormalizeShareInfo 规整并检查：不合格（标题为空、
 *  超长）时窗口不关，说明行改为提醒色的提示；合格时向通知窗口（屏幕共享控制器）PostMessage kMsgShareInfoEditResult，
 *  lParam 为堆上的 ShareInfoEditResult*（由控制器释放）。Esc、「取消」或关窗则 lParam 为 0。
 *  服务端以敏感词拒绝（720）时，控制器带着刚才输入的内容与提示文字再打开一次本窗口。
 *
 *  形态：balloonui DuiFrameWindow 的 WS_POPUP 小窗，置顶、不进任务栏，居中在被共享内容所在的显示器上，排除在屏幕采集之外
 *  （发起人的桌面正被共享，本窗口不应出现在观看者的画面里）。不设所有者窗口：balloonui 把按钮通知发给 GetParent 返回的
 *  窗口，弹出窗口设了所有者，通知就会发到所有者那里（见 ShareAnnotationBar.h）。内容全部用 balloonui 现成控件组合。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（UI 线程）：
 *      m_infoEdit.Open(monitorRef, m_hWnd, m_hostTitle, m_hostDesc, std::wstring());
 *      //控制器收到 kMsgShareInfoEditResult 后：
 *      std::unique_ptr<ShareInfoEditResult> r(reinterpret_cast<ShareInfoEditResult*>(lParam));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace balloonwjui
{
    class DuiEdit;
    class DuiLabel;
}

namespace screenshare {

//「保存」的结果：已按 NormalizeShareInfo 规整（去掉首尾空白）并检查合格
struct ShareInfoEditResult
{
    std::wstring m_title;   //新的共享标题，1～kShareTitleMaxChars 字
    std::wstring m_desc;    //新的共享描述，最多 kShareDescMaxChars 字，可以为空
};

//「修改标题与描述」小窗
class CShareInfoEditDialog : public balloonwjui::DuiFrameWindow
{
public:
    CShareInfoEditDialog();
    ~CShareInfoEditDialog();

    /**
     *  打开小窗并让标题输入框取得键盘焦点；已打开时先按取消关闭旧的。
     *  @param monitorRef 定位用的窗口（发起人控制条），只用来确定显示器、不作为所有者；为 NULL 时居中在主显示器上
     *  @param notifyWnd  通知窗口，不拥有
     *  @param title      标题输入框的初始内容
     *  @param desc       描述输入框的初始内容
     *  @param errorHint  说明行显示的提醒文字（如服务端以敏感词拒绝后的提示）；为空时显示普通的填写说明
     */
    void Open(HWND monitorRef, HWND notifyWnd, const std::wstring& title, const std::wstring& desc,
              const std::wstring& errorHint);

    //不发结果直接关闭；可重复调用（共享结束时由控制器调用）
    void Close();

    //小窗是否开着
    bool IsShown() const;

    BEGIN_MSG_MAP(CShareInfoEditDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //输入框与按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
    enum
    {
        kIdTitleEdit = 10,   //标题输入框
        kIdDescEdit  = 11,   //描述输入框
        kIdSave      = 12,   //保存
        kIdCancel    = 13,   //取消
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //发出结果并关窗：ok 为 false 时发 0；ok 为 true 而输入不合格时不关窗，说明行改为提醒
    void finish(bool ok);

    HWND                   m_notifyWnd;   //通知窗口（不拥有）
    balloonwjui::DuiEdit*  m_titleEdit;   //标题输入框（控件树持有，本指针仅引用）
    balloonwjui::DuiEdit*  m_descEdit;    //描述输入框（同上）
    balloonwjui::DuiLabel* m_hintLabel;   //说明行（同上）
    bool                   m_finished;    //本次是否已发过结果
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
