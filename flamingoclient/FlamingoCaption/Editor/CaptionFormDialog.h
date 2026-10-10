/**
 *  字幕编辑器的通用小表单对话框，CaptionFormDialog.h
 *
 *  查找替换、整体平移、批量改说话人、字幕外观都是「几行标签 + 输入框或勾选项，底部几个按钮」的小窗口，共用本类：
 *  调用方依次 AddEdit / AddCheck / AddNote 加行、AddButton 加按钮，再 Run 以模态方式显示（禁用属主窗口、自带消息循环，
 *  与 MessageBox 的用法相同）。点按钮时交给 ICaptionFormHandler 决定是否关闭（查找替换的「查找下一处」不关闭）；
 *  没有给处理对象时点任何按钮都关闭。只用 balloonui 现成控件（DuiLabel、DuiEdit、DuiButton），不自绘。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      CaptionFormDialog dlg;
 *      const int field = dlg.AddEdit(TR(L"平移秒数"), L"0.0");
 *      dlg.AddButton(TR(L"确定"), true);
 *      dlg.AddButton(TR(L"取消"), false);
 *      if (dlg.Run(m_hWnd, TR(L"整体平移"), NULL) == 0) ... dlg.Text(field) ...
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiButton;
    class DuiEdit;
    class DuiLabel;
}

namespace caption {

class CaptionFormDialog;

/**
 *  表单按钮的处理对象。
 */
class ICaptionFormHandler
{
public:
    /**
     *  点了第 button 个按钮（UI 线程）。可以读写表单里的值（如更新说明行）。
     *  @return 关闭对话框返回 true
     */
    virtual bool OnFormButton(int button, CaptionFormDialog& form) = 0;

protected:
    ~ICaptionFormHandler()
    {
    }
};

/**
 *  CaptionFormDialog —— 通用小表单对话框（一次性使用：Run 返回后不再复用）。
 */
class CaptionFormDialog : public balloonwjui::DuiFrameWindow
{
public:
    CaptionFormDialog();
    ~CaptionFormDialog();

    /**
     *  加一行输入框。
     *  @return 行号（读写这一行用）
     */
    int AddEdit(const std::wstring& label, const std::wstring& text);

    //加一行勾选项；返回行号
    int AddCheck(const std::wstring& label, bool checked);

    //加一行说明文字（灰色，可在运行中经 SetNote 修改）；返回行号
    int AddNote(const std::wstring& text);

    /**
     *  加一个按钮（从左到右排在底部右侧）。
     *  @param primary 是否为主按钮（蓝色）；回车等同于点第一个主按钮
     *  @return 按钮序号（Run 的返回值与 OnFormButton 的参数）
     */
    int AddButton(const std::wstring& text, bool primary);

    /**
     *  以模态方式显示，直到关闭。
     *  @param owner   属主窗口（显示期间被禁用，对话框居中在它上面）
     *  @param title   标题
     *  @param handler 按钮处理对象（不拥有）；为 NULL 时点任何按钮都关闭
     *  @return 关闭时点的按钮序号；点标题栏关闭或按 Esc 时返回 -1
     */
    int Run(HWND owner, const std::wstring& title, ICaptionFormHandler* handler);

    //某一行输入框的文字
    std::wstring Text(int field) const;

    //某一行勾选项是否勾上
    bool Checked(int field) const;

    //改某一行说明文字
    void SetNote(int field, const std::wstring& text);

    BEGIN_MSG_MAP(CaptionFormDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //一行
    struct Field
    {
        int                     m_kind;     //种类：0 输入框，1 勾选项，2 说明文字
        std::wstring            m_label;    //标签（勾选项为勾选项文字，说明文字为正文）
        std::wstring            m_text;     //输入框初始文字；关闭后为最终文字
        bool                    m_checked;  //勾选项是否勾上（关闭后为最终状态）
        balloonwjui::DuiEdit*   m_edit;     //输入框（窗口存在期间有效）
        balloonwjui::DuiButton* m_check;    //勾选项（同上）
        balloonwjui::DuiLabel*  m_note;     //说明文字（同上）
    };

    //一个按钮
    struct Button
    {
        std::wstring m_text;     //文字
        bool         m_primary;  //是否为主按钮
    };

    void buildUi();
    //把控件里的值存回 m_fields（关闭前调用）
    void collect();
    //结束模态：记下结果、存值、销毁窗口
    void finish(int result);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    std::vector<Field>    m_fields;    //各行
    std::vector<Button>   m_buttons;   //各按钮
    ICaptionFormHandler*  m_handler;   //按钮处理对象（不拥有）
    bool                  m_done;      //模态是否已结束
    int                   m_result;    //结束时点的按钮；-1 表示取消
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（及任务栏）的程序图标；本类持有，窗口关闭再打开时复用
};

/**
 *  模态消息循环：禁用属主、显示窗口，处理消息直到 done 为 true 或窗口被销毁，再恢复属主并激活它。
 *  @param dlg   对话框窗口
 *  @param owner 属主窗口（可为 NULL）
 *  @param done  结束标志（对话框在关闭时置 true）
 */
void RunCaptionModalLoop(HWND dlg, HWND owner, const bool& done);

} // namespace caption
