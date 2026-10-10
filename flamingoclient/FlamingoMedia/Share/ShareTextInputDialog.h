/**
 *  屏幕共享模块的单行文字输入小窗，ShareTextInputDialog.h
 *
 *  一个可选的说明行、一个单行输入框加「确定」「取消」。回车或「确定」向通知窗口（屏幕共享控制器）PostMessage
 *  调用方指定的结果消息，lParam 为堆上的 std::wstring*（由接收方释放）；Esc、「取消」或关窗则 lParam 为 0。
 *  确定时可按调用方给的校验函数检查输入，不通过时窗口不关，说明行改为红色的提示文字。
 *  现有两处用法：
 *    - 标注文字：标注工具选了「文字」后在共享内容上点一下，在点击处弹出（Open 的第一种重载），结果消息为
 *      kMsgShareAnnotText；发起人一侧的输入框浮在被共享的内容上，排除在屏幕采集之外；
 *    - 加入密码（第 12 期）：加入设了密码的共享时，居中弹出输入 6 位数字密码（OpenCentered），结果消息为
 *      kMsgSharePasswordResult。
 *
 *  形态：balloonui DuiFrameWindow 的 WS_POPUP 小窗，一律不设所有者窗口：balloonui 把按钮通知发给 GetParent 返回的窗口，
 *  弹出窗口设了所有者，「确定」「取消」的通知就会发到所有者那里（见 ShareAnnotationBar.h）。标注文字置顶、不进任务栏；
 *  加入密码不置顶（发起人可能要切到别的程序查看密码），改为在任务栏上显示带程序图标的按钮，被别的窗口盖住后能找回。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiEdit;
    class DuiLabel;
}

namespace screenshare {

//确定时的校验函数：输入合格返回 true
typedef bool (*ShareTextValidator)(const std::wstring& text);

//输入小窗的用法参数
struct ShareTextInputSpec
{
    std::wstring       m_title;         //标题栏文字
    std::wstring       m_hint;          //输入框上方的说明文字；为空且 m_invalidHint 也为空时不显示说明行
    bool               m_hintIsError;   //说明文字是否用提醒色（如「密码不对，请重新输入」）
    std::wstring       m_placeholder;   //输入框为空时显示的提示文字
    std::wstring       m_invalidHint;   //确定时校验不通过，说明行改显示的文字（提醒色）
    int                m_maxChars;      //最多可输入的字符数
    ShareTextValidator m_validator;     //确定时的校验函数；为 NULL 时只要求不为空
    UINT               m_resultMsg;     //结果消息号，投给通知窗口
    bool               m_topmost;       //是否置顶
    bool               m_inTaskbar;     //是否在任务栏上显示按钮（任务栏与 Alt+Tab 用程序图标）；为 false 时不进任务栏。标题栏不论取值都显示程序图标
    std::wstring       m_initialText;   //打开时输入框里预填的文字（第 20 期，修改已有文字时用），预填后全选
    bool               m_allowEmpty;    //没有校验函数时，确定时允许为空（第 20 期，清空图元名字时用）；为空时结果为空串

    ShareTextInputSpec();
};

/**
 *  双击修改一个标注对象的文字时输入框的参数（第 21 期从屏幕共享控制器抽出，控制器与本机标注共用）：文字（清空即删除）、图元名字
 *  （可以为空）、数字标号号码（只接受 1～65535 的数）、连接线上的文字（可以为空）各有标题与提示；预填现有文字，结果消息为
 *  kMsgShareAnnotText。
 *  @param tool    对象的种类（AnnotationTool）
 *  @param current 现有的文字（数字标号为号码）
 */
ShareTextInputSpec MakeAnnotTextEditSpec(int tool, const std::wstring& current);

//屏幕共享模块的单行文字输入小窗
class CShareTextInputDialog : public balloonwjui::DuiFrameWindow
{
public:
    CShareTextInputDialog();
    ~CShareTextInputDialog();

    /**
     *  打开标注文字的输入框：在屏幕坐标 at 处（放不下时挪进工作区）打开并取得键盘焦点；已打开时先按取消关闭旧的。
     *  结果消息为 kMsgShareAnnotText。
     *  @param at          文字左上角对应的屏幕坐标
     *  @param notifyWnd   通知窗口，不拥有
     *  @param excludeFromCapture 是否排除在屏幕采集之外（发起人传 true）
     */
    void Open(POINT at, HWND notifyWnd, bool excludeFromCapture);

    /**
     *  同上，另给用法参数（第 20 期，双击修改已有文字时用：预填现有文字、标题与提示不同）。
     *  @param spec 用法参数
     */
    void Open(POINT at, HWND notifyWnd, bool excludeFromCapture, const ShareTextInputSpec& spec);

    /**
     *  按给定参数打开输入框，居中在 anchor 所在显示器的工作区上；已打开时先按取消关闭旧的。
     *  @param anchor    定位用的窗口（exe 主窗口），只用来确定显示器、不作为所有者；为 NULL 时居中在主显示器上
     *  @param notifyWnd 通知窗口，不拥有
     *  @param spec      用法参数
     */
    void OpenCentered(HWND anchor, HWND notifyWnd, const ShareTextInputSpec& spec);

    //不发结果直接关闭；可重复调用
    void Close();

    BEGIN_MSG_MAP(CShareTextInputDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //按钮与输入框的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
    enum
    {
        kIdEdit   = 10,   //输入框
        kIdOk     = 11,   //确定
        kIdCancel = 12,   //取消
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //建窗口并摆到 (x, y)（窗口左上角，屏幕坐标），取得键盘焦点
    void openAt(int x, int y, HWND notifyWnd, bool excludeFromCapture, const ShareTextInputSpec& spec);
    //标题栏换成程序图标；taskIcons 为 true（窗口在任务栏上显示）时任务栏与 Alt+Tab 也换成程序图标（图标只建一次，本类持有）
    void applyProgramIcons(bool taskIcons);
    //按当前参数算出的整窗高度（有说明行时更高）
    int dialogHeight() const;
    //发出结果并关窗：ok 为 false 时发 0；ok 为 true 而输入为空或校验不通过时不关窗
    void finish(bool ok);

    HWND                   m_notifyWnd;   //通知窗口（不拥有）
    ShareTextInputSpec     m_spec;        //本次打开的用法参数
    balloonwjui::DuiEdit*  m_edit;        //输入框（控件树持有，本指针仅引用）
    balloonwjui::DuiLabel* m_hintLabel;   //说明行（同上）；不显示说明行时为 NULL
    bool                   m_finished;    //本次是否已发过结果
    HBITMAP                m_titleIcon;   //标题栏的程序图标位图（各种用途都使用）；本类持有，析构时释放
    HICON                  m_taskIconBig; //任务栏 / Alt+Tab 大图标（m_inTaskbar 时使用）；本类持有，析构时 DestroyIcon
    HICON                  m_taskIconSmall;   //任务栏小图标（同上）
};

} // namespace screenshare
