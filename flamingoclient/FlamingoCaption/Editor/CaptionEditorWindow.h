/**
 *  字幕编辑器窗口（docs/语音字幕方案.md 第 7 节），CaptionEditorWindow.h
 *
 *  入口：录完提示的「编辑字幕」、录制管理窗口的「编辑字幕」（经 ICaptionModule::OpenEditor）。一个录像一个窗口，
 *  同一个录像再次打开时激活已有的窗口。窗口布局：
 *    - 顶部工具栏：播放 / 暂停、当前时刻与时长、识别字幕（或重新识别、取消识别）、查找替换、整体平移、改说话人、
 *      外观、替换表、撤销、重做、导出；
 *    - 左侧上方视频预览（CaptionPreviewView，画面上叠加当前字幕，单击切换播放），下方时间轴（CaptionTimelineView）；
 *    - 右侧字幕列表（DuiListBox：主文字为「说话人：文字」，副文字为起止时刻），下面是选中字幕的编辑区：
 *      开始、结束（各带 ±0.1 秒微调）、说话人、文字，以及「应用」「拆分」「合并下一条」「插入」「删除」；
 *    - 底部状态栏：保存状态、识别与预览的进度。
 *  数据与编辑规则在 CaptionEditorModel；改动后 2 秒自动保存，关窗时保存。分段式录像先在后台原样转封装一份普通 MP4
 *  供预览（14.7），预览准备期间照样可以编辑。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（字幕模块里）：
 *      std::unique_ptr<CaptionEditorWindow> w(new CaptionEditorWindow());
 *      if (w->Open(owner, videoPath)) m_editors.push_back(std::move(w));
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CaptionEditorMessages.h"
#include "CaptionEditorModel.h"
#include "CaptionPostRecognizer.h"
#include "CaptionVideoPlayer.h"
#include "../Export/CaptionTimedText.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiButton;
    class DuiEdit;
    class DuiLabel;
    class DuiListBox;
    class DuiRichEdit;
}

namespace caption {

class CaptionPreviewView;
class CaptionTimelineView;

/**
 *  CaptionEditorWindow —— 一个录像的字幕编辑器窗口。只在 UI 线程上使用。
 */
class CaptionEditorWindow : public balloonwjui::DuiFrameWindow
{
public:
    CaptionEditorWindow();
    ~CaptionEditorWindow();

    /**
     *  打开窗口：载入草稿、建界面、开始准备预览。
     *  @param owner     属主窗口（可为 NULL）；窗口居中在它所在的显示器上
     *  @param videoPath 录像完整路径
     *  @return 文件不存在或建窗口失败时返回 false
     */
    bool Open(HWND owner, const std::wstring& videoPath);

    //恢复（最小化了的）并激活
    void Activate();

    //保存并关闭（模块关闭时调用）；可重复调用
    void CloseEditor();

    //窗口是否开着
    bool IsOpen() const;

    //对应的录像路径
    const std::wstring& VideoPath() const;

    BEGIN_MSG_MAP(CaptionEditorWindow)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(kMsgCaptionPlayer, OnPlayerMsg)
        MESSAGE_HANDLER(kMsgCaptionPostRec, OnPostRecMsg)
        MESSAGE_HANDLER(kMsgCaptionDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //---- 界面 ----
    void buildUi();
    //按模型重建字幕列表，并把时间轴、预览字幕一并刷新
    void reloadList();
    //按选中项刷新列表与时间轴的选中、编辑区的内容
    void refreshSelection();
    //编辑区的内容与选中字幕是否不同（用户改了还没应用）
    bool panelModified() const;
    //刷新工具栏各按钮的文字与可用状态
    void updateToolbar();
    //刷新时刻文字与状态栏
    void updateTimeText();
    void setStatus(const std::wstring& text);
    //按当前播放位置刷新预览上的字幕
    void updateCaptionOverlay();
    //任何改动之后：重建列表、安排自动保存、刷新工具栏
    void afterEdit();
    void scheduleSave();
    void saveNow();

    //---- 播放 ----
    void preparePreview();
    void previewThreadProc(std::wstring source, std::wstring target);
    void togglePlay();
    void seekTo(int64_t ms);
    //从播放器取一帧交给预览区；force 为 true 时即使没有新帧也重取
    void grabFrame(bool force);
    //文件里没有画面（录音，14.13）：预览区改为大字显示当前字幕，隐藏「外观」按钮
    void enterAudioOnlyMode();
    int64_t currentMs() const;

    //---- 编辑操作 ----
    void applyEdits();
    void nudge(bool start, int64_t deltaMs);
    void splitSelected();
    void mergeSelected();
    void insertAtPlayhead();
    void deleteSelected();
    void undo();
    void redo();
    void showFindReplace();
    void showShift();
    void showRenameSpeaker();
    void showStyle();
    void showReplaceTable();
    void showExport();
    void startOrCancelRecognize();

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPlayerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPostRecMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    CaptionEditorModel            m_model;          //字幕数据
    std::vector<CaptionSpan>      m_spans;          //显示区间（预览叠字幕用，每次改动后重算）
    CaptionVideoPlayer            m_player;         //视频预览播放器
    CaptionPostRecognizer         m_recognizer;     //事后识别
    std::unique_ptr<std::thread>  m_previewThread;  //准备预览（转封装分段式录像）的后台线程
    std::wstring                  m_previewPath;    //实际交给播放器的文件（转封装的临时文件或原录像）
    bool                          m_previewIsTemp;  //m_previewPath 是不是临时文件（关窗时删除）
    std::vector<uint32_t>         m_frame;          //从播放器取到的帧
    int                           m_frameW;         //上次取帧的宽
    int                           m_frameH;         //上次取帧的高
    int                           m_seekTicks;      //跳转后还要连续取帧的次数
    int64_t                       m_durationMs;     //录像时长（预览就绪后取自播放器，否则取自草稿或字幕）
    bool                          m_closing;        //正在关闭
    bool                          m_audioOnly;      //打开的是没有画面的录音（14.13）：预览就绪后由播放器的画面尺寸判定
    //控件（归控件树所有，窗口销毁后为 NULL）
    CaptionPreviewView*           m_preview;
    CaptionTimelineView*          m_timeline;
    balloonwjui::DuiListBox*      m_list;
    balloonwjui::DuiEdit*         m_startEdit;
    balloonwjui::DuiEdit*         m_endEdit;
    balloonwjui::DuiEdit*         m_speakerEdit;
    balloonwjui::DuiRichEdit*     m_textEdit;
    balloonwjui::DuiButton*       m_playBtn;
    balloonwjui::DuiButton*       m_recognizeBtn;
    balloonwjui::DuiButton*       m_undoBtn;
    balloonwjui::DuiButton*       m_redoBtn;
    balloonwjui::DuiButton*       m_applyBtn;
    balloonwjui::DuiButton*       m_splitBtn;
    balloonwjui::DuiButton*       m_mergeBtn;
    balloonwjui::DuiButton*       m_deleteBtn;
    balloonwjui::DuiButton*       m_styleBtn;       //「外观」：录音（14.13）没有画面，隐藏
    balloonwjui::DuiLabel*        m_timeLabel;
    balloonwjui::DuiLabel*        m_statusLabel;
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（及任务栏）的程序图标；本类持有，窗口关闭再打开时复用
};

} // namespace caption
