/**
 *  字幕编辑器的导出窗口（docs/语音字幕方案.md 第 8 节、第 2 节 D1），CaptionExportDialog.h
 *
 *  三个勾选项：硬字幕版「原名-字幕.mp4」、字幕轨版「原名-字幕轨.mp4」、外挂字幕「原名.srt」；第一次只勾硬字幕版，之后
 *  记住上次的勾选（存在按账号的字幕设置里）。目标文件已存在时先询问是否覆盖。点「开始导出」后在后台导出
 *  （CaptionExportJob），显示进度，可以取消；结束后列出每一项的结果，可「打开所在文件夹」或「发送到会话…」
 *  （优先发硬字幕版，其次字幕轨版，最后 SRT）。以模态方式显示（与 CaptionFormDialog 相同的消息循环）。
 *  导出成功的几项由调用方写进草稿的导出记录（录制管理窗口据此显示「已导出字幕」）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      CaptionExportDialog dlg;
 *      std::vector<CaptionExportItemResult> done;
 *      dlg.Run(m_hWnd, model.ToDraft(), model.VideoPath(), done);
 *      for (...) model.AddExport(done[i].m_kind, FileNameOf(done[i].m_path));
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

#include "../Export/CaptionExportJob.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "CaptionEditorMessages.h"

namespace balloonwjui
{
    class DuiButton;
    class DuiLabel;
    class DuiProgressBar;
}

namespace caption {

/**
 *  CaptionExportDialog —— 导出窗口（一次性使用）。
 */
class CaptionExportDialog : public balloonwjui::DuiFrameWindow, private ICaptionExportListener
{
public:
    CaptionExportDialog();
    ~CaptionExportDialog();

    /**
     *  以模态方式显示，直到关闭。
     *  @param owner     属主窗口（编辑器）
     *  @param draft     当前的字幕与外观
     *  @param videoPath 原录像路径
     *  @param done      [出参] 导出成功的各项（可能为空）
     */
    void Run(HWND owner, const CaptionDraft& draft, const std::wstring& videoPath,
             std::vector<CaptionExportItemResult>& done);

    BEGIN_MSG_MAP(CaptionExportDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(kMsgCaptionExportProgress, OnProgressMsg)
        MESSAGE_HANDLER(kMsgCaptionExportDone, OnDoneMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    void buildUi();
    //点了「开始导出」：确认覆盖、记住勾选、开始后台导出
    void start();
    //按状态刷新各按钮的可用状态
    void updateButtons();
    //打开所在文件夹、发送到会话
    void openFolder();
    void sendToConversation();
    //结束模态
    void finish();
    //ICaptionExportListener：后台线程上转投给窗口
    virtual void OnExportProgress(int kind, int permille, int overall) override;
    virtual void OnExportFinished(const CaptionExportResult& result) override;
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnProgressMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    CaptionDraft                          m_draft;       //当前的字幕与外观（Run 时拷贝）
    std::wstring                          m_videoPath;   //原录像路径
    CaptionExportJob                      m_job;         //后台导出
    std::vector<CaptionExportItemResult>  m_done;        //已成功的各项
    bool                                  m_finished;    //是否导出过（结束后才能打开文件夹、发送）
    bool                                  m_closeAfter;  //导出中点了关闭：取消后等结束再关
    bool                                  m_modalDone;   //模态是否已结束
    bool                                  m_audioOnly;   //导出的是没有画面的录音（14.13）：只提供 SRT 与纯文本
    balloonwjui::DuiButton*               m_hardCheck;   //「硬字幕版」勾选项（归控件树所有）；录音时没有，为 NULL
    balloonwjui::DuiButton*               m_trackCheck;  //「字幕轨版」勾选项；录音时没有，为 NULL
    balloonwjui::DuiButton*               m_srtCheck;    //「SRT」勾选项
    balloonwjui::DuiButton*               m_textCheck;   //「纯文本」勾选项（14.13）
    balloonwjui::DuiProgressBar*          m_progress;    //进度条
    balloonwjui::DuiLabel*                m_status;      //状态文字
    balloonwjui::DuiButton*               m_startBtn;    //「开始导出」
    balloonwjui::DuiButton*               m_folderBtn;   //「打开所在文件夹」
    balloonwjui::DuiButton*               m_sendBtn;     //「发送到会话…」
    balloonwjui::DuiButton*               m_closeBtn;    //「关闭」/「取消」
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（及任务栏）的程序图标；本类持有，窗口关闭再打开时复用
};

} // namespace caption
