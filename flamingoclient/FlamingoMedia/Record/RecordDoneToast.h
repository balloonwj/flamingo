/**
 *  录制完成的提示，RecordDoneToast.h
 *
 *  录制停止、文件保存好之后在屏幕右下角弹出（docs/屏幕共享与录制方案.md 6.2「录完之后」）：三行文字（「录制完成」或
 *  自动停止的原因、文件名、时长与大小，文件名一行为第 14 期新增）+「打开」「打开所在文件夹」「打开我的录制」（2026-10-06
 *  新增，打开录制管理窗口）「发送」（从聊天窗发起的录制）或「发送到会话…」（从主面板、托盘或快捷键发起，点了再选会话），
 *  这次录制生成了字幕草稿时多一个「编辑字幕」（docs/语音字幕方案.md 第 2 节第 9 条）。窗口 400×228，按钮分两行（2026-10-06）：
 *  第一行靠左依次是「打开」「打开所在文件夹」「打开我的录制」，第二行左侧是「编辑字幕」、右侧是发送按钮，有没有「编辑字幕」
 *  窗口大小都不变。点按钮向通知窗口（录屏控制器）投
 *  kMsgRecordDoneCommand，由控制器处理；只在点了标题栏关闭或「发送」之后关闭，不会自动关闭（2026-10-05 用户要求去掉倒计时，
 *  录完常常要过一阵才回来处理）。独立录屏与屏幕共享中的录制共用。
 *
 *  形态与屏幕共享的右下角提示（CShareCornerToast）一致：balloonui DuiFrameWindow 的小窗，置顶、不抢焦点、不进任务栏，
 *  落点用 exe 与 DLL 共用的 balloonmain::MoveWindowToWorkAreaCorner。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"           // ProgramWindowIcons（标题栏程序图标）

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace screenrecord {

//录制完成的提示
class CRecordDoneToast : public balloonwjui::DuiFrameWindow
{
public:
    //按钮命令（kMsgRecordDoneCommand 的 wParam）
    enum Command
    {
        kCmdPlay       = 1,   //「打开」：用系统默认的播放器打开
        kCmdOpenFolder = 2,   //「打开所在文件夹」：打开所在文件夹并选中文件
        kCmdSend       = 3,   //发送到会话
        kCmdOpenManager = 4,  //「打开我的录制」：打开录制管理窗口（2026-10-06）
        kCmdEditCaption = 5,  //「编辑字幕」：打开这次录像的字幕编辑器（语音字幕，2026-10-06）
    };

    //按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）；公开是为了单元测试按 id 查找按钮
    enum
    {
        kIdPlay    = 10,   //打开
        kIdFolder  = 11,   //打开所在文件夹
        kIdSend    = 12,   //发送
        kIdManager = 13,   //打开我的录制（2026-10-06）
        kIdCaption = 14,   //编辑字幕（语音字幕，2026-10-06）
    };

    CRecordDoneToast();
    ~CRecordDoneToast();

    /**
     *  弹出提示（已弹出时替换内容）。
     *  @param title     第一行（如「录制完成」「磁盘空间不足，录制已自动停止」）
     *  @param fileName  第二行，录像的文件名（不含目录，见 RecordFileNameOf）
     *  @param detail    第三行（时长与大小，见 FormatRecordDoneDetail）
     *  @param sendText  发送按钮的文字（「发送」或「发送到会话…」）
     *  @param notifyWnd 通知窗口，不拥有
     *  @param showCaption 是否显示「编辑字幕」按钮（这次录制生成了字幕草稿、且字幕功能可用时为 true）
     */
    void Show(const std::wstring& title, const std::wstring& fileName, const std::wstring& detail,
              const std::wstring& sendText, HWND notifyWnd, bool showCaption = false);

    //关闭；可重复调用
    void Hide();

    BEGIN_MSG_MAP(CRecordDoneToast)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void send(int command);

    HWND m_notifyWnd;   //通知窗口（不拥有）
    balloonmain::ProgramWindowIcons m_icons;      //标题栏的程序图标（第 15 期），本对象持有；窗口先于它销毁
};

} // namespace screenrecord
