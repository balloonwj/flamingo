/**
 *  字幕编辑器窗口，见 CaptionEditorWindow.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionEditorWindow.h"

#include <objbase.h>
#include <mfapi.h>
#include <stdlib.h>

#include "CaptionExportDialog.h"
#include "CaptionFormDialog.h"
#include "CaptionPreviewView.h"
#include "CaptionReplaceDialog.h"
#include "CaptionTimelineView.h"
#include "../Engine/CaptionEngine.h"
#include "../Export/CaptionMediaIo.h"
#include "../Export/Mp4TextTrack.h"   //ListMp4TopLevelBoxes：判断是不是分段式 MP4
#include "../Host/CaptionModule.h"
#include "../Model/CaptionFiles.h"
#include "../Model/CaptionSettings.h"
#include "../Model/CaptionText.h"
#include "../../Source/Common/I18n/I18n.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"

namespace caption {

namespace {

//---- 尺寸（逻辑像素；整窗尺寸含标题栏，见开发约定文档 UI 约定第 7 条）----
const int kDefaultW = 1180;
const int kDefaultH = 760;
const int kMinW = 960;
const int kMinH = 620;
const int kTitleBarH = 32;
const int kPadding = 12;
const int kGap = 8;
const int kToolbarH = 32;
const int kTimelineH = 100;
const int kRightW = 400;
const int kRowH = 30;
const int kTextEditH = 96;
const int kStatusH = 22;
const int kListItemH = 44;
const int kFieldLabelW = 48;
const int kTimeEditW = 96;
const int kNudgeW = 44;
const int kTimeLabelW = 150;
//按钮宽度：文字宽度加左右空白，不小于最小宽度
const int kButtonTextExtra = 24;
const int kButtonMinW = 56;
//定时器：取帧（约 30 次/秒）、自动保存
const UINT_PTR kTimerFrame = 701;
const UINT kFrameMs = 33;
const UINT_PTR kTimerSave = 702;
const UINT kSaveDelayMs = 2000;
//跳转后连续取帧的次数（0e 实测跳转后 14～220 毫秒拿到新帧）
const int kSeekTicks = 15;
//微调的步长（毫秒）
const int64_t kNudgeMs = 100;
//千分比换成百分比
const int kPermillePerPercent = 10;
//按钮测宽用的字号（像素，与 balloonui 默认字体一致）
const int kMeasureFontPx = 14;
//外观的取值范围（像素，按 1080 高的画面计）
const int kMinFontPx = 16;
const int kMaxFontPx = 120;
const int kMaxMarginPx = 400;
const int kMaxOutlinePx = 20;
const int kMaxBoxAlpha = 255;
//平移秒数的上限（绝对值，秒）
const double kMaxShiftSec = 3600.0;
const double kMsPerSecond = 1000.0;
//状态栏文字颜色
const COLORREF kStatusColor = RGB(110, 114, 122);
//预览临时文件所在的子目录（系统临时目录下）
const wchar_t* const kPreviewTempDir = L"FlamingoCaption\\";

//延后处理的动作（kMsgCaptionDeferred 的 wParam）
enum DeferredAction
{
    defer_preview_ready = 1,   //预览文件已准备好：lParam 为 1 成功、0 失败
    defer_list_select   = 2,   //列表选中变了：lParam 为下标
    defer_timeline_drag = 3,   //时间轴拖动结束：取拖动结果交给模型
};

//控件 id（避开标题栏按钮占用的 1~3）
enum ControlId
{
    kIdPlay = 101,
    kIdTime = 102,
    kIdRecognize = 103,
    kIdFind = 104,
    kIdShift = 105,
    kIdSpeakers = 106,
    kIdStyle = 107,
    kIdReplace = 108,
    kIdUndo = 109,
    kIdRedo = 110,
    kIdExport = 111,
    kIdPreview = 120,
    kIdTimeline = 121,
    kIdList = 130,
    kIdStartEdit = 140,
    kIdEndEdit = 141,
    kIdStartMinus = 142,
    kIdStartPlus = 143,
    kIdEndMinus = 144,
    kIdEndPlus = 145,
    kIdSpeakerEdit = 146,
    kIdTextEdit = 147,
    kIdApply = 150,
    kIdSplit = 151,
    kIdMerge = 152,
    kIdInsert = 153,
    kIdDelete = 154,
    kIdStatus = 160,
};

//按当前界面字体量文字宽度
int TextWidth(const std::wstring& text)
{
    HDC dc = ::GetDC(NULL);
    HFONT font = ::CreateFontW(-kMeasureFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               i18n::UiFontName());
    HGDIOBJ old = ::SelectObject(dc, font);
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    ::SelectObject(dc, old);
    ::DeleteObject(font);
    ::ReleaseDC(NULL, dc);
    return size.cx;
}

int ButtonWidth(const std::wstring& text)
{
    const int w = TextWidth(text) + kButtonTextExtra;
    return w > kButtonMinW ? w : kButtonMinW;
}

//建一个按钮并加进横排
balloonwjui::DuiButton* AddButton(balloonwjui::DuiHBox* row, const std::wstring& text, UINT id, bool primary)
{
    std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
    b->SetText(text.c_str());
    b->SetVariant(primary ? balloonwjui::DuiButton::Variant::Primary : balloonwjui::DuiButton::Variant::Default);
    b->SetCtrlId(id);
    balloonwjui::DuiButton* raw = b.get();
    row->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(ButtonWidth(text)));
    return raw;
}

//建一个单行输入框
std::unique_ptr<balloonwjui::DuiEdit> MakeEdit(UINT id)
{
    std::unique_ptr<balloonwjui::DuiEdit> e(new balloonwjui::DuiEdit());
    e->SetCtrlId(id);
    e->SetVerticalCenter(true);
    e->SetShowBorder(true);
    return e;
}

//建一个左对齐的标签
std::unique_ptr<balloonwjui::DuiLabel> MakeLabel(const std::wstring& text)
{
    std::unique_ptr<balloonwjui::DuiLabel> l(new balloonwjui::DuiLabel());
    l->SetText(text.c_str());
    l->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    return l;
}

//把整数文字解析出来并夹在范围内；解析不了返回 false
bool ParseIntIn(const std::wstring& text, int lo, int hi, int& out)
{
    wchar_t* end = NULL;
    const long v = wcstol(text.c_str(), &end, 10);
    if (end == text.c_str())
        return false;
    out = static_cast<int>(v < lo ? lo : (v > hi ? hi : v));
    return true;
}

//查找替换对话框的按钮处理：「查找下一处」不关闭，「全部替换」替换后在说明行报告结果
class FindReplaceHandler : public ICaptionFormHandler
{
public:
    FindReplaceHandler(CaptionEditorModel& model, int findField, int replaceField, int caseField, int noteField)
        : m_model(model)
        , m_findField(findField)
        , m_replaceField(replaceField)
        , m_caseField(caseField)
        , m_noteField(noteField)
        , m_changed(false)
        , m_found(false)
    {
    }

    virtual bool OnFormButton(int button, CaptionFormDialog& form) override
    {
        const std::wstring needle = form.Text(m_findField);
        const bool matchCase = form.Checked(m_caseField);
        //按钮依次为：查找下一处、全部替换、关闭
        switch (button)
        {
        //查找下一处：选中找到的那条，对话框保留
        case 0:
            if (m_model.FindNext(needle, matchCase) >= 0)
            {
                m_found = true;
                form.SetNote(m_noteField, TR(L"已选中找到的字幕。"));
            }
            else
            {
                form.SetNote(m_noteField, TR(L"没有找到。"));
            }
            return false;
        //全部替换：报告替换次数，对话框保留
        case 1:
        {
            const int count = m_model.ReplaceAll(needle, form.Text(m_replaceField), matchCase);
            if (count > 0)
                m_changed = true;
            form.SetNote(m_noteField, TRF(L"已替换 {0} 处。", count));
            return false;
        }
        //关闭
        default:
            return true;
        }
    }

    bool Changed() const
    {
        return m_changed;
    }

    bool Found() const
    {
        return m_found;
    }

private:
    CaptionEditorModel& m_model;        //编辑数据（借用）
    int                 m_findField;    //「查找」行号
    int                 m_replaceField; //「替换为」行号
    int                 m_caseField;    //「区分大小写」行号
    int                 m_noteField;    //说明行号
    bool                m_changed;      //是否替换过
    bool                m_found;        //是否找到过（选中项变了）
};

} // namespace

CaptionEditorWindow::CaptionEditorWindow()
    : m_previewIsTemp(false)
    , m_frameW(0)
    , m_frameH(0)
    , m_seekTicks(0)
    , m_durationMs(0)
    , m_closing(false)
    , m_audioOnly(false)
    , m_preview(NULL)
    , m_timeline(NULL)
    , m_list(NULL)
    , m_startEdit(NULL)
    , m_endEdit(NULL)
    , m_speakerEdit(NULL)
    , m_textEdit(NULL)
    , m_playBtn(NULL)
    , m_recognizeBtn(NULL)
    , m_undoBtn(NULL)
    , m_redoBtn(NULL)
    , m_applyBtn(NULL)
    , m_splitBtn(NULL)
    , m_mergeBtn(NULL)
    , m_deleteBtn(NULL)
    , m_styleBtn(NULL)
    , m_timeLabel(NULL)
    , m_statusLabel(NULL)
{
}

CaptionEditorWindow::~CaptionEditorWindow()
{
    CloseEditor();
}

bool CaptionEditorWindow::Open(HWND owner, const std::wstring& videoPath)
{
    if (::GetFileAttributesW(videoPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LOG_WARNING("caption editor: video file does not exist.");
        return false;
    }
    std::string error;
    const bool draftOk = m_model.Load(videoPath, &error);
    if (!draftOk)
        LOG_WARNING("caption editor: draft unreadable (%s), starting empty.", error.c_str());
    const std::wstring title = TRF(L"字幕编辑 - {0}", FileNameOf(videoPath));
    if (Create(owner, CWindow::rcDefault, title.c_str(), WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW) == NULL)
    {
        LOG_ERROR("caption editor: create window failed.");
        return false;
    }
    SetTitle(title.c_str());
    SetTitleBarHeight(kTitleBarH);
    //标题栏与任务栏显示程序图标（开发约定文档「UI 约定」第 10 条）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetButtons(true, true, true);
    SetResizable(true);
    SetMinSize(kMinW, kMinH);
    ResizeClient(kDefaultW, kDefaultH);
    buildUi();
    //居中在属主所在的显示器上
    HMONITOR mon = ::MonitorFromWindow(owner != NULL ? owner : m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
    {
        const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - kDefaultW) / 2;
        const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - kDefaultH) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    //草稿里记着时长就先用它（预览就绪后改用播放器给的）；没有时用最后一条字幕的结束时刻
    m_durationMs = m_model.ToDraft().m_videoDurationMs;
    if (m_durationMs <= 0 && m_model.Track().Count() > 0)
        m_durationMs = m_model.Track().At(m_model.Track().Count() - 1).m_endMs;
    if (m_model.Track().Count() > 0)
        m_model.Select(0);
    reloadList();
    updateToolbar();
    updateTimeText();
    setStatus(draftOk ? TR(L"字幕已载入。") : TR(L"字幕草稿读取失败，已从空白字幕开始；保存时会覆盖原草稿。"));
    ShowWindow(SW_SHOW);
    Activate();
    SetTimer(kTimerFrame, kFrameMs, NULL);
    //提前加载识别模型：点「识别字幕」时多半已经加载好
    CaptionEngine::Instance().Preload();
    preparePreview();
    LOG_INFO("caption editor: opened, %u cues.", static_cast<unsigned>(m_model.Track().Count()));
    return true;
}

void CaptionEditorWindow::Activate()
{
    if (!IsWindow())
        return;
    if (IsIconic())
        ShowWindow(SW_RESTORE);
    ::SetForegroundWindow(m_hWnd);
}

bool CaptionEditorWindow::IsOpen() const
{
    return ::IsWindow(m_hWnd) != FALSE;
}

const std::wstring& CaptionEditorWindow::VideoPath() const
{
    return m_model.VideoPath();
}

void CaptionEditorWindow::CloseEditor()
{
    if (m_closing)
        return;
    m_closing = true;
    if (IsWindow())
    {
        KillTimer(kTimerFrame);
        KillTimer(kTimerSave);
    }
    if (panelModified())
        applyEdits();
    if (m_model.IsDirty())
        saveNow();
    m_recognizer.Cancel();
    m_recognizer.Wait();
    m_player.Close();
    if (m_previewThread)
    {
        if (m_previewThread->joinable())
            m_previewThread->join();
        m_previewThread.reset();
    }
    if (m_previewIsTemp && !m_previewPath.empty())
        ::DeleteFileW(m_previewPath.c_str());
    m_preview = NULL;
    m_timeline = NULL;
    m_list = NULL;
    m_startEdit = NULL;
    m_endEdit = NULL;
    m_speakerEdit = NULL;
    m_textEdit = NULL;
    m_playBtn = NULL;
    m_recognizeBtn = NULL;
    m_undoBtn = NULL;
    m_redoBtn = NULL;
    m_applyBtn = NULL;
    m_splitBtn = NULL;
    m_mergeBtn = NULL;
    m_deleteBtn = NULL;
    m_styleBtn = NULL;
    m_timeLabel = NULL;
    m_statusLabel = NULL;
    if (IsWindow())
        DestroyWindow();
    LOG_INFO("caption editor: closed.");
}

//==================== 界面 ====================

void CaptionEditorWindow::buildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);

    //工具栏
    std::unique_ptr<balloonwjui::DuiHBox> bar(new balloonwjui::DuiHBox());
    bar->SetGap(kGap);
    m_playBtn = AddButton(bar.get(), TR(L"播放"), kIdPlay, true);
    std::unique_ptr<balloonwjui::DuiLabel> time = MakeLabel(L"");
    time->SetCtrlId(kIdTime);
    m_timeLabel = time.get();
    bar->AddChild(std::move(time), balloonwjui::DuiLayout::Hint().Fixed(kTimeLabelW));
    bar->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));
    m_recognizeBtn = AddButton(bar.get(), TR(L"重新识别"), kIdRecognize, false);
    AddButton(bar.get(), TR(L"查找替换"), kIdFind, false);
    AddButton(bar.get(), TR(L"整体平移"), kIdShift, false);
    AddButton(bar.get(), TR(L"改说话人"), kIdSpeakers, false);
    m_styleBtn = AddButton(bar.get(), TR(L"外观"), kIdStyle, false);
    AddButton(bar.get(), TR(L"替换表"), kIdReplace, false);
    m_undoBtn = AddButton(bar.get(), TR(L"撤销"), kIdUndo, false);
    m_redoBtn = AddButton(bar.get(), TR(L"重做"), kIdRedo, false);
    AddButton(bar.get(), TR(L"导出…"), kIdExport, true);
    root->AddChild(std::move(bar), balloonwjui::DuiLayout::Hint().Fixed(kToolbarH));

    //主体：左边预览与时间轴，右边列表与编辑区
    std::unique_ptr<balloonwjui::DuiHBox> body(new balloonwjui::DuiHBox());
    body->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiVBox> left(new balloonwjui::DuiVBox());
    left->SetGap(kGap);
    std::unique_ptr<CaptionPreviewView> preview(new CaptionPreviewView());
    preview->SetCtrlId(kIdPreview);
    preview->SetMessage(TR(L"正在准备预览…"));
    m_preview = preview.get();
    left->AddChild(std::move(preview), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<CaptionTimelineView> timeline(new CaptionTimelineView());
    timeline->SetCtrlId(kIdTimeline);
    m_timeline = timeline.get();
    left->AddChild(std::move(timeline), balloonwjui::DuiLayout::Hint().Fixed(kTimelineH));
    body->AddChild(std::move(left), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiVBox> right(new balloonwjui::DuiVBox());
    right->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiListBox> list(new balloonwjui::DuiListBox());
    list->SetCtrlId(kIdList);
    list->SetItemHeight(kListItemH);
    m_list = list.get();
    right->AddChild(std::move(list), balloonwjui::DuiLayout::Hint().Weight(1));
    //开始
    std::unique_ptr<balloonwjui::DuiHBox> startRow(new balloonwjui::DuiHBox());
    startRow->SetGap(kGap);
    startRow->AddChild(MakeLabel(TR(L"开始")), balloonwjui::DuiLayout::Hint().Fixed(kFieldLabelW));
    std::unique_ptr<balloonwjui::DuiEdit> startEdit = MakeEdit(kIdStartEdit);
    m_startEdit = startEdit.get();
    startRow->AddChild(std::move(startEdit), balloonwjui::DuiLayout::Hint().Fixed(kTimeEditW));
    std::unique_ptr<balloonwjui::DuiButton> sm(new balloonwjui::DuiButton());
    sm->SetText(L"-0.1");
    sm->SetCtrlId(kIdStartMinus);
    startRow->AddChild(std::move(sm), balloonwjui::DuiLayout::Hint().Fixed(kNudgeW));
    std::unique_ptr<balloonwjui::DuiButton> sp(new balloonwjui::DuiButton());
    sp->SetText(L"+0.1");
    sp->SetCtrlId(kIdStartPlus);
    startRow->AddChild(std::move(sp), balloonwjui::DuiLayout::Hint().Fixed(kNudgeW));
    right->AddChild(std::move(startRow), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    //结束
    std::unique_ptr<balloonwjui::DuiHBox> endRow(new balloonwjui::DuiHBox());
    endRow->SetGap(kGap);
    endRow->AddChild(MakeLabel(TR(L"结束")), balloonwjui::DuiLayout::Hint().Fixed(kFieldLabelW));
    std::unique_ptr<balloonwjui::DuiEdit> endEdit = MakeEdit(kIdEndEdit);
    m_endEdit = endEdit.get();
    endRow->AddChild(std::move(endEdit), balloonwjui::DuiLayout::Hint().Fixed(kTimeEditW));
    std::unique_ptr<balloonwjui::DuiButton> em(new balloonwjui::DuiButton());
    em->SetText(L"-0.1");
    em->SetCtrlId(kIdEndMinus);
    endRow->AddChild(std::move(em), balloonwjui::DuiLayout::Hint().Fixed(kNudgeW));
    std::unique_ptr<balloonwjui::DuiButton> ep(new balloonwjui::DuiButton());
    ep->SetText(L"+0.1");
    ep->SetCtrlId(kIdEndPlus);
    endRow->AddChild(std::move(ep), balloonwjui::DuiLayout::Hint().Fixed(kNudgeW));
    right->AddChild(std::move(endRow), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    //说话人
    std::unique_ptr<balloonwjui::DuiHBox> speakerRow(new balloonwjui::DuiHBox());
    speakerRow->SetGap(kGap);
    speakerRow->AddChild(MakeLabel(TR(L"说话人")), balloonwjui::DuiLayout::Hint().Fixed(kFieldLabelW));
    std::unique_ptr<balloonwjui::DuiEdit> speakerEdit = MakeEdit(kIdSpeakerEdit);
    m_speakerEdit = speakerEdit.get();
    speakerRow->AddChild(std::move(speakerEdit), balloonwjui::DuiLayout::Hint().Weight(1));
    right->AddChild(std::move(speakerRow), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    //文字
    std::unique_ptr<balloonwjui::DuiRichEdit> textEdit(new balloonwjui::DuiRichEdit());
    textEdit->SetCtrlId(kIdTextEdit);
    textEdit->SetMultiLine(true);
    textEdit->SetWordWrap(true);
    textEdit->SetShowBorder(true);
    textEdit->SetPlaceholder(TR(L"字幕文字"));
    m_textEdit = textEdit.get();
    right->AddChild(std::move(textEdit), balloonwjui::DuiLayout::Hint().Fixed(kTextEditH));
    //操作按钮
    std::unique_ptr<balloonwjui::DuiHBox> ops(new balloonwjui::DuiHBox());
    ops->SetGap(kGap);
    m_applyBtn = AddButton(ops.get(), TR(L"应用"), kIdApply, true);
    m_splitBtn = AddButton(ops.get(), TR(L"拆分"), kIdSplit, false);
    m_mergeBtn = AddButton(ops.get(), TR(L"合并下一条"), kIdMerge, false);
    AddButton(ops.get(), TR(L"插入"), kIdInsert, false);
    m_deleteBtn = AddButton(ops.get(), TR(L"删除"), kIdDelete, false);
    right->AddChild(std::move(ops), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    body->AddChild(std::move(right), balloonwjui::DuiLayout::Hint().Fixed(kRightW));
    root->AddChild(std::move(body), balloonwjui::DuiLayout::Hint().Weight(1));

    //状态栏
    std::unique_ptr<balloonwjui::DuiLabel> status = MakeLabel(L"");
    status->SetCtrlId(kIdStatus);
    status->SetTextColor(kStatusColor);
    m_statusLabel = status.get();
    root->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Fixed(kStatusH));
    SetClientContent(std::move(root));
}

void CaptionEditorWindow::reloadList()
{
    if (m_list == NULL)
        return;
    const CaptionTrack& track = m_model.Track();
    m_list->DeleteAllItems();
    const std::wstring editedMark = TR(L"已修改");
    for (size_t i = 0; i < track.Count(); ++i)
    {
        const int idx = m_list->AddItem(CueListMainText(track.At(i)).c_str(), static_cast<LPARAM>(track.At(i).m_id));
        m_list->SetItemSubText(idx, CueListSubText(track.At(i), editedMark).c_str());
    }
    m_spans = BuildDisplaySpans(track.Cues());
    if (m_timeline != NULL)
    {
        m_timeline->SetCues(track.Cues());
        int64_t duration = m_durationMs;
        if (track.Count() > 0 && track.At(track.Count() - 1).m_endMs > duration)
            duration = track.At(track.Count() - 1).m_endMs;
        m_timeline->SetDuration(duration);
    }
    refreshSelection();
    updateCaptionOverlay();
}

void CaptionEditorWindow::refreshSelection()
{
    const int sel = m_model.Selected();
    if (m_list != NULL)
    {
        m_list->SetCurSel(sel, false);
        if (sel >= 0)
            m_list->EnsureVisible(sel);
    }
    if (m_timeline != NULL)
        m_timeline->SetSelected(sel);
    const bool has = (sel >= 0);
    if (m_startEdit != NULL)
    {
        const CaptionCue* cue = has ? &m_model.Track().At(static_cast<size_t>(sel)) : NULL;
        m_startEdit->SetTextNoNotify(cue != NULL ? FormatEditTime(cue->m_startMs).c_str() : L"");
        m_endEdit->SetTextNoNotify(cue != NULL ? FormatEditTime(cue->m_endMs).c_str() : L"");
        m_speakerEdit->SetTextNoNotify(cue != NULL ? cue->m_speaker.c_str() : L"");
        m_textEdit->SetText(cue != NULL ? cue->m_text.c_str() : L"");
    }
    if (m_applyBtn != NULL)
    {
        m_applyBtn->SetEnabled(has);
        m_splitBtn->SetEnabled(has);
        m_mergeBtn->SetEnabled(has && sel + 1 < static_cast<int>(m_model.Track().Count()));
        m_deleteBtn->SetEnabled(has);
    }
}

bool CaptionEditorWindow::panelModified() const
{
    const int sel = m_model.Selected();
    if (sel < 0 || m_textEdit == NULL)
        return false;
    const CaptionCue& cue = m_model.Track().At(static_cast<size_t>(sel));
    if (NormalizeText(m_textEdit->GetText().GetString()) != cue.m_text)
        return true;
    if (NormalizeText(m_speakerEdit->GetText().GetString()) != cue.m_speaker)
        return true;
    if (std::wstring(m_startEdit->GetText().GetString()) != FormatEditTime(cue.m_startMs))
        return true;
    return std::wstring(m_endEdit->GetText().GetString()) != FormatEditTime(cue.m_endMs);
}

void CaptionEditorWindow::updateToolbar()
{
    if (m_playBtn == NULL)
        return;
    m_playBtn->SetText(m_player.IsPlaying() ? TR(L"暂停") : TR(L"播放"));
    m_playBtn->SetEnabled(m_player.IsReady());
    if (m_recognizer.IsRunning())
        m_recognizeBtn->SetText(TR(L"取消识别"));
    else
        m_recognizeBtn->SetText(m_model.Track().Count() > 0 ? TR(L"重新识别") : TR(L"识别字幕"));
    m_undoBtn->SetEnabled(m_model.CanUndo());
    m_redoBtn->SetEnabled(m_model.CanRedo());
}

int64_t CaptionEditorWindow::currentMs() const
{
    return m_player.IsReady() ? m_player.PositionMs() : 0;
}

void CaptionEditorWindow::updateTimeText()
{
    if (m_timeLabel == NULL)
        return;
    const std::wstring text = FormatEditTime(currentMs()) + L" / " + FormatEditTime(m_durationMs);
    m_timeLabel->SetText(text.c_str());
}

void CaptionEditorWindow::setStatus(const std::wstring& text)
{
    if (m_statusLabel != NULL)
        m_statusLabel->SetText(text.c_str());
}

void CaptionEditorWindow::updateCaptionOverlay()
{
    if (m_preview == NULL)
        return;
    const int idx = FindSpanAt(m_spans, currentMs());
    if (idx < 0)
        m_preview->SetCaption(L"", L"", m_model.Style(), m_model.Pip());
    else
        m_preview->SetCaption(m_spans[idx].m_speaker, m_spans[idx].m_text, m_model.Style(), m_model.Pip());
}

void CaptionEditorWindow::afterEdit()
{
    reloadList();
    updateToolbar();
    scheduleSave();
}

void CaptionEditorWindow::scheduleSave()
{
    if (IsWindow())
        SetTimer(kTimerSave, kSaveDelayMs, NULL);
    setStatus(TR(L"有未保存的修改…"));
}

void CaptionEditorWindow::saveNow()
{
    if (IsWindow())
        KillTimer(kTimerSave);
    std::string error;
    if (m_model.Save(&error))
    {
        setStatus(TR(L"已保存。"));
    }
    else
    {
        LOG_ERROR("caption editor: save draft failed: %s.", error.c_str());
        setStatus(TR(L"保存失败，请检查录像所在的文件夹是否可写。"));
    }
}

//==================== 播放 ====================

void CaptionEditorWindow::preparePreview()
{
    const std::wstring source = m_model.VideoPath();
    //分段式 MP4（Flamingo 录像）先在后台原样转封装一份普通 MP4 再预览（14.7：分段式上跳转不可靠）
    bool fragmented = false;
    const std::vector<std::string> boxes = ListMp4TopLevelBoxes(source);
    for (size_t i = 0; i < boxes.size() && !fragmented; ++i)
        fragmented = (boxes[i] == "moof");
    if (!fragmented)
    {
        m_previewPath = source;
        m_previewIsTemp = false;
        PostMessage(kMsgCaptionDeferred, defer_preview_ready, 1);
        return;
    }
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    std::wstring dir = std::wstring(tempDir) + kPreviewTempDir;
    ::CreateDirectoryW(dir.c_str(), NULL);
    wchar_t name[MAX_PATH] = { 0 };
    _snwprintf_s(name, MAX_PATH, _TRUNCATE, L"preview_%lu_%p.mp4", ::GetCurrentProcessId(), this);
    m_previewPath = dir + name;
    m_previewIsTemp = true;
    setStatus(TR(L"正在准备预览…"));
    m_previewThread.reset(new std::thread(&CaptionEditorWindow::previewThreadProc, this, source, m_previewPath));
}

void CaptionEditorWindow::previewThreadProc(std::wstring source, std::wstring target)
{
    const HRESULT co = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const HRESULT mf = ::MFStartup(MF_VERSION);
    std::string error;
    const bool ok = RemuxMp4(source, target, NULL, &error);
    if (!ok)
        LOG_WARNING("caption editor: preview remux failed (%s), previewing the original file.", error.c_str());
    if (SUCCEEDED(mf))
        ::MFShutdown();
    if (SUCCEEDED(co))
        ::CoUninitialize();
    HWND wnd = m_hWnd;
    if (::IsWindow(wnd))
        ::PostMessage(wnd, kMsgCaptionDeferred, defer_preview_ready, ok ? 1 : 0);
}

void CaptionEditorWindow::togglePlay()
{
    if (!m_player.IsReady())
        return;
    if (m_player.IsPlaying())
        m_player.Pause();
    else
        m_player.Play();
    updateToolbar();
}

void CaptionEditorWindow::seekTo(int64_t ms)
{
    if (!m_player.IsReady())
        return;
    m_player.Seek(ms);
    m_seekTicks = kSeekTicks;
    if (m_timeline != NULL)
        m_timeline->SetPosition(ms, false);
    updateTimeText();
    updateCaptionOverlay();
}

void CaptionEditorWindow::enterAudioOnlyMode()
{
    LOG_INFO("caption editor: the file has no video stream, showing captions in audio mode.");
    if (m_preview != NULL)
    {
        m_preview->ClearFrame();
        m_preview->SetAudioOnly(true);
        m_preview->SetMessage(TR(L"录音（没有画面）"));
    }
    //外观只影响画面上的字幕，录音没有画面；SetVisible 不会重新排版，要对所在的那一行强制排一次（开发约定文档「UI 约定」第 5 条）
    if (m_styleBtn != NULL && m_styleBtn->IsVisible())
    {
        m_styleBtn->SetVisible(false);
        balloonwjui::DuiControl* row = m_styleBtn->GetParent();
        if (row != NULL)
            row->ForceLayout(row->GetRect());
    }
}

void CaptionEditorWindow::grabFrame(bool force)
{
    //没有画面的录音不取帧
    if (m_preview == NULL || !m_player.IsReady() || m_audioOnly)
        return;
    const RECT rc = m_preview->GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return;
    const bool resized = (w != m_frameW || h != m_frameH);
    if (!m_player.GrabFrame(static_cast<uint32_t>(w), static_cast<uint32_t>(h), m_frame, force || resized))
        return;
    m_frameW = w;
    m_frameH = h;
    m_preview->SetFrame(m_frame, w, h, m_player.VideoRectIn(static_cast<uint32_t>(w), static_cast<uint32_t>(h)));
}

//==================== 编辑操作 ====================

void CaptionEditorWindow::applyEdits()
{
    const int sel = m_model.Selected();
    if (sel < 0 || m_textEdit == NULL)
        return;
    int64_t start = 0;
    int64_t end = 0;
    if (!ParseEditTime(m_startEdit->GetText().GetString(), start) ||
        !ParseEditTime(m_endEdit->GetText().GetString(), end))
    {
        ::MessageBoxW(m_hWnd, TR(L"时间格式不对，请写成「分:秒.十分之一秒」，如 01:23.4。"), TR(L"字幕编辑"),
                      MB_OK | MB_ICONWARNING);
        return;
    }
    if (end - start < kCaptionMinCueMs)
    {
        ::MessageBoxW(m_hWnd, TR(L"结束时刻要比开始时刻晚至少 0.2 秒。"), TR(L"字幕编辑"), MB_OK | MB_ICONWARNING);
        return;
    }
    bool changed = false;
    //依次应用文字、说话人、时间；每一项按编号重新取下标（改时间后会重新排序）
    if (m_model.EditText(m_model.Selected(), m_textEdit->GetText().GetString()))
        changed = true;
    if (m_model.EditSpeaker(m_model.Selected(), m_speakerEdit->GetText().GetString()))
        changed = true;
    if (m_model.EditTimes(m_model.Selected(), start, end))
        changed = true;
    if (changed)
        afterEdit();
    else
        refreshSelection();
}

void CaptionEditorWindow::nudge(bool start, int64_t deltaMs)
{
    const int sel = m_model.Selected();
    if (sel < 0)
        return;
    const CaptionCue& cue = m_model.Track().At(static_cast<size_t>(sel));
    const int64_t s = start ? cue.m_startMs + deltaMs : cue.m_startMs;
    const int64_t e = start ? cue.m_endMs : cue.m_endMs + deltaMs;
    if (m_model.EditTimes(sel, s, e))
    {
        afterEdit();
        if (start)
            seekTo(s);
    }
}

void CaptionEditorWindow::splitSelected()
{
    const int sel = m_model.Selected();
    if (sel < 0 || m_textEdit == NULL)
        return;
    //先应用编辑区里没应用的文字，再按光标位置拆分
    if (panelModified())
        applyEdits();
    long cpMin = 0;
    long cpMax = 0;
    m_textEdit->GetSel(cpMin, cpMax);
    if (cpMin <= 0 || !m_model.Split(m_model.Selected(), static_cast<size_t>(cpMin)))
    {
        setStatus(TR(L"请把光标放在文字中要拆开的位置，两半各自不能短于 0.2 秒。"));
        return;
    }
    afterEdit();
}

void CaptionEditorWindow::mergeSelected()
{
    if (panelModified())
        applyEdits();
    if (m_model.MergeWithNext(m_model.Selected()))
        afterEdit();
}

void CaptionEditorWindow::insertAtPlayhead()
{
    if (panelModified())
        applyEdits();
    if (m_model.Insert(currentMs(), TR(L"（新字幕）")))
    {
        afterEdit();
        if (m_textEdit != NULL)
            m_textEdit->SetFocus();
    }
    else
    {
        setStatus(TR(L"这里离下一条字幕太近，放不下新字幕。"));
    }
}

void CaptionEditorWindow::deleteSelected()
{
    if (m_model.Remove(m_model.Selected()))
        afterEdit();
}

void CaptionEditorWindow::undo()
{
    if (m_model.Undo())
        afterEdit();
}

void CaptionEditorWindow::redo()
{
    if (m_model.Redo())
        afterEdit();
}

void CaptionEditorWindow::showFindReplace()
{
    CaptionFormDialog dlg;
    const int findField = dlg.AddEdit(TR(L"查找"), L"");
    const int replaceField = dlg.AddEdit(TR(L"替换为"), L"");
    const int caseField = dlg.AddCheck(TR(L"区分大小写（英文）"), false);
    const int noteField = dlg.AddNote(L"");
    dlg.AddButton(TR(L"查找下一处"), true);
    dlg.AddButton(TR(L"全部替换"), false);
    dlg.AddButton(TR(L"关闭"), false);
    FindReplaceHandler handler(m_model, findField, replaceField, caseField, noteField);
    dlg.Run(m_hWnd, TR(L"查找替换"), &handler);
    if (handler.Changed())
        afterEdit();
    else if (handler.Found())
        refreshSelection();
}

void CaptionEditorWindow::showShift()
{
    CaptionFormDialog dlg;
    const int field = dlg.AddEdit(TR(L"平移秒数"), L"0.0");
    dlg.AddNote(TR(L"正数整体往后移，负数整体往前移，如 -1.5 表示全部提前 1.5 秒。"));
    dlg.AddButton(TR(L"确定"), true);
    dlg.AddButton(TR(L"取消"), false);
    if (dlg.Run(m_hWnd, TR(L"整体平移"), NULL) != 0)
        return;
    const double sec = _wtof(dlg.Text(field).c_str());
    if (sec == 0.0 || sec > kMaxShiftSec || sec < -kMaxShiftSec)
        return;
    if (m_model.ShiftAll(static_cast<int64_t>(sec * kMsPerSecond)))
        afterEdit();
}

void CaptionEditorWindow::showRenameSpeaker()
{
    const std::vector<std::wstring> speakers = m_model.Speakers();
    std::wstring list;
    for (size_t i = 0; i < speakers.size(); ++i)
    {
        if (!list.empty())
            list += TR(L"、");
        list += speakers[i];
    }
    CaptionFormDialog dlg;
    const int fromField = dlg.AddEdit(TR(L"原说话人"), speakers.empty() ? std::wstring() : speakers[0]);
    const int toField = dlg.AddEdit(TR(L"改为"), L"");
    dlg.AddNote(speakers.empty() ? std::wstring(TR(L"目前没有带说话人的字幕。"))
                                 : TRF(L"现有的说话人：{0}", list));
    dlg.AddButton(TR(L"确定"), true);
    dlg.AddButton(TR(L"取消"), false);
    if (dlg.Run(m_hWnd, TR(L"改说话人"), NULL) != 0)
        return;
    const int count = m_model.RenameSpeaker(NormalizeText(dlg.Text(fromField)), dlg.Text(toField));
    if (count > 0)
    {
        afterEdit();
        setStatus(TRF(L"已改 {0} 条字幕的说话人。", count));
    }
}

void CaptionEditorWindow::showStyle()
{
    const CaptionStyle current = m_model.Style();
    CaptionFormDialog dlg;
    wchar_t buf[16];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", current.m_fontPx);
    const int fontField = dlg.AddEdit(TR(L"字号"), buf);
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", current.m_bottomMarginPx);
    const int marginField = dlg.AddEdit(TR(L"离底边"), buf);
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", current.m_outlinePx);
    const int outlineField = dlg.AddEdit(TR(L"描边宽度"), buf);
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", current.m_boxAlpha);
    const int boxField = dlg.AddEdit(TR(L"底框不透明度"), buf);
    const int speakerField = dlg.AddCheck(TR(L"显示说话人"), current.m_showSpeaker);
    dlg.AddNote(TR(L"尺寸按 1080 高的画面计，导出时按实际画面高度缩放；底框不透明度 0～255，0 表示不画底框。"));
    dlg.AddButton(TR(L"确定"), true);
    dlg.AddButton(TR(L"取消"), false);
    if (dlg.Run(m_hWnd, TR(L"字幕外观"), NULL) != 0)
        return;
    CaptionStyle style = current;
    ParseIntIn(dlg.Text(fontField), kMinFontPx, kMaxFontPx, style.m_fontPx);
    ParseIntIn(dlg.Text(marginField), 0, kMaxMarginPx, style.m_bottomMarginPx);
    ParseIntIn(dlg.Text(outlineField), 0, kMaxOutlinePx, style.m_outlinePx);
    ParseIntIn(dlg.Text(boxField), 0, kMaxBoxAlpha, style.m_boxAlpha);
    style.m_showSpeaker = dlg.Checked(speakerField);
    m_model.SetStyle(style);
    updateCaptionOverlay();
    scheduleSave();
}

void CaptionEditorWindow::showReplaceTable()
{
    CaptionSettings settings = CCaptionModule::Instance().GetSettings();
    CaptionReplaceDialog dlg(settings.m_replace);
    if (!dlg.Run(m_hWnd))
        return;
    settings.m_replace = dlg.Table();
    CCaptionModule::Instance().UpdateSettings(settings);
    setStatus(TR(L"替换表已保存，之后识别的字幕会自动替换。"));
}

void CaptionEditorWindow::showExport()
{
    if (panelModified())
        applyEdits();
    if (m_model.Track().Count() == 0)
    {
        ::MessageBoxW(m_hWnd, TR(L"还没有字幕，请先识别字幕或插入字幕。"), TR(L"导出字幕"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (m_player.IsPlaying())
        togglePlay();
    CaptionExportDialog dlg;
    std::vector<CaptionExportItemResult> done;
    dlg.Run(m_hWnd, m_model.ToDraft(), m_model.VideoPath(), done);
    if (done.empty())
        return;
    for (size_t i = 0; i < done.size(); ++i)
        m_model.AddExport(done[i].m_kind, FileNameOf(done[i].m_path));
    saveNow();
}

void CaptionEditorWindow::startOrCancelRecognize()
{
    if (m_recognizer.IsRunning())
    {
        m_recognizer.Cancel();
        setStatus(TR(L"正在取消识别…"));
        return;
    }
    if (m_model.Track().Count() > 0 &&
        ::MessageBoxW(m_hWnd, TR(L"重新识别会替换现有的全部字幕（可以撤销），继续吗？"), TR(L"重新识别"),
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    const CaptionSettings settings = CCaptionModule::Instance().GetSettings();
    if (!m_recognizer.Start(m_model.VideoPath(), settings.m_replace, m_hWnd, kMsgCaptionPostRec))
        return;
    setStatus(TR(L"正在识别字幕…"));
    updateToolbar();
}

//==================== 消息 ====================

LRESULT CaptionEditorWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //列表选中变了：留到列表的鼠标事件处理完再刷新（刷新时可能重建列表）
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) && n->ctrlId == kIdList)
    {
        PostMessage(kMsgCaptionDeferred, defer_list_select, n->extra);
        return 0;
    }
    //双击列表：跳到那条字幕
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_DBLCLK) && n->ctrlId == kIdList)
    {
        const int idx = static_cast<int>(n->extra);
        if (idx >= 0 && idx < static_cast<int>(m_model.Track().Count()))
            seekTo(m_model.Track().At(static_cast<size_t>(idx)).m_startMs);
        return 0;
    }
    //时间轴：点空白处跳转、点色块选中、拖动结束
    if (n->ctrlId == kIdTimeline)
    {
        if (n->code == static_cast<UINT>(CaptionTimelineView::DUIN_TIMELINE_SEEK))
        {
            seekTo(static_cast<int64_t>(n->extra));
            return 0;
        }
        if (n->code == static_cast<UINT>(CaptionTimelineView::DUIN_TIMELINE_SELECT))
        {
            PostMessage(kMsgCaptionDeferred, defer_list_select, n->extra);
            return 0;
        }
        if (n->code == static_cast<UINT>(CaptionTimelineView::DUIN_TIMELINE_DRAGGED))
        {
            PostMessage(kMsgCaptionDeferred, defer_timeline_drag, 0);
            return 0;
        }
    }
    //单行输入框里按回车：应用
    if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) &&
        (n->ctrlId == kIdStartEdit || n->ctrlId == kIdEndEdit || n->ctrlId == kIdSpeakerEdit))
    {
        applyEdits();
        return 0;
    }
    if (n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    //播放 / 暂停（工具栏按钮或单击预览区）
    case kIdPlay:
    case kIdPreview:
        togglePlay();
        return 0;
    //识别字幕、重新识别或取消识别
    case kIdRecognize:
        startOrCancelRecognize();
        return 0;
    //查找替换
    case kIdFind:
        showFindReplace();
        return 0;
    //整体平移
    case kIdShift:
        showShift();
        return 0;
    //批量改说话人
    case kIdSpeakers:
        showRenameSpeaker();
        return 0;
    //字幕外观
    case kIdStyle:
        showStyle();
        return 0;
    //自动替换表
    case kIdReplace:
        showReplaceTable();
        return 0;
    //撤销
    case kIdUndo:
        undo();
        return 0;
    //重做
    case kIdRedo:
        redo();
        return 0;
    //导出
    case kIdExport:
        showExport();
        return 0;
    //开始时刻 -0.1 / +0.1 秒
    case kIdStartMinus:
        nudge(true, -kNudgeMs);
        return 0;
    case kIdStartPlus:
        nudge(true, kNudgeMs);
        return 0;
    //结束时刻 -0.1 / +0.1 秒
    case kIdEndMinus:
        nudge(false, -kNudgeMs);
        return 0;
    case kIdEndPlus:
        nudge(false, kNudgeMs);
        return 0;
    //应用编辑区
    case kIdApply:
        applyEdits();
        return 0;
    //在光标处拆分
    case kIdSplit:
        splitSelected();
        return 0;
    //与下一条合并
    case kIdMerge:
        mergeSelected();
        return 0;
    //在播放位置插入
    case kIdInsert:
        insertAtPlayhead();
        return 0;
    //删除
    case kIdDelete:
        deleteSelected();
        return 0;
    //其它控件
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CaptionEditorWindow::OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (m_closing)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //预览文件准备好了：交给播放器打开（转封装失败时直接打开原录像）
    case defer_preview_ready:
    {
        if (m_previewThread)
        {
            if (m_previewThread->joinable())
                m_previewThread->join();
            m_previewThread.reset();
        }
        if (lParam == 0 && m_previewIsTemp)
        {
            ::DeleteFileW(m_previewPath.c_str());
            m_previewPath = m_model.VideoPath();
            m_previewIsTemp = false;
        }
        if (!m_player.Open(m_previewPath, m_hWnd, kMsgCaptionPlayer))
        {
            if (m_preview != NULL)
                m_preview->SetMessage(TR(L"无法预览这个录像，字幕仍可编辑与导出。"));
            setStatus(TR(L"预览不可用。"));
        }
        return 0;
    }
    //列表或时间轴选中了某条：先应用编辑区里没应用的改动，再刷新编辑区并跳到它
    case defer_list_select:
    {
        const int idx = static_cast<int>(lParam);
        if (idx < 0 || idx >= static_cast<int>(m_model.Track().Count()))
            return 0;
        const int64_t targetId = m_model.Track().At(static_cast<size_t>(idx)).m_id;
        if (panelModified())
            applyEdits();
        const int now = m_model.Track().IndexOfId(targetId);
        m_model.Select(now);
        refreshSelection();
        if (now >= 0)
            seekTo(m_model.Track().At(static_cast<size_t>(now)).m_startMs);
        return 0;
    }
    //时间轴拖动结束：新的起止时刻交给模型（可撤销）
    case defer_timeline_drag:
    {
        int index = -1;
        int64_t start = 0;
        int64_t end = 0;
        if (m_timeline != NULL && m_timeline->DragResult(index, start, end) && m_model.EditTimes(index, start, end))
        {
            m_model.Select(m_model.Track().IndexOfId(m_model.SelectedId()));
            afterEdit();
        }
        return 0;
    }
    //没有其它动作
    default:
        return 0;
    }
}

LRESULT CaptionEditorWindow::OnPlayerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    if (m_closing)
        return 0;
    switch (static_cast<int>(wParam))
    {
    //可以播放：取时长，显示第一帧
    case caption_player_ready:
    {
        const int64_t duration = m_player.DurationMs();
        if (duration > 0)
            m_durationMs = duration;
        if (m_timeline != NULL)
            m_timeline->SetDuration(m_durationMs);
        m_seekTicks = kSeekTicks;
        //没有画面的录音（14.13，屏幕共享方案第 24 期只录声音）：预览区改为大字显示当前字幕，隐藏「外观」
        uint32_t videoW = 0;
        uint32_t videoH = 0;
        m_audioOnly = !m_player.VideoSize(videoW, videoH) || videoW == 0 || videoH == 0;
        if (m_audioOnly)
            enterAudioOnlyMode();
        else
            grabFrame(true);
        updateToolbar();
        updateTimeText();
        updateCaptionOverlay();
        setStatus(m_audioOnly ? TR(L"录音已就绪。单击预览区播放或暂停。") : TR(L"预览已就绪。单击画面播放或暂停。"));
        return 0;
    }
    //跳转完成：继续连续取帧直到拿到新帧
    case caption_player_seeked:
        m_seekTicks = kSeekTicks;
        return 0;
    //播放到结尾
    case caption_player_ended:
        updateToolbar();
        return 0;
    //出错：预览不可用，字幕照常编辑
    case caption_player_error:
        if (m_preview != NULL)
        {
            m_preview->ClearFrame();
            m_preview->SetMessage(TR(L"无法预览这个录像，字幕仍可编辑与导出。"));
        }
        updateToolbar();
        return 0;
    //其它事件
    default:
        return 0;
    }
}

LRESULT CaptionEditorWindow::OnPostRecMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (m_closing)
        return 0;
    if (static_cast<int>(wParam) == caption_postrec_progress)
    {
        setStatus(TRF(L"正在识别字幕… {0}%", static_cast<int>(lParam) / kPermillePerPercent));
        return 0;
    }
    //结束：成功时用结果整体替换全部字幕（可撤销）
    m_recognizer.Wait();
    std::vector<CaptionCue> cues;
    bool cancelled = false;
    std::string error;
    if (m_recognizer.TakeResult(cues, cancelled, error))
    {
        m_model.ReplaceAllCues(cues);
        if (!cues.empty())
            m_model.Select(0);
        afterEdit();
        setStatus(cues.empty() ? std::wstring(TR(L"识别完成，没有识别出字幕。"))
                               : TRF(L"识别完成，共 {0} 条字幕。", static_cast<int>(cues.size())));
    }
    else if (cancelled)
    {
        setStatus(TR(L"已取消识别。"));
    }
    else
    {
        LOG_WARNING("caption editor: post recognition failed: %s.", error.c_str());
        setStatus(TR(L"识别失败：语音识别组件不可用。"));
    }
    updateToolbar();
    return 0;
}

LRESULT CaptionEditorWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam == kTimerSave)
    {
        saveNow();
        return 0;
    }
    if (wParam != kTimerFrame)
    {
        bHandled = FALSE;
        return 0;
    }
    if (m_closing || !m_player.IsReady())
        return 0;
    const bool playing = m_player.IsPlaying();
    if (playing || m_seekTicks > 0 || m_preview == NULL ||
        m_preview->GetRect().right - m_preview->GetRect().left != m_frameW ||
        m_preview->GetRect().bottom - m_preview->GetRect().top != m_frameH)
    {
        grabFrame(false);
        if (m_seekTicks > 0)
            --m_seekTicks;
    }
    if (playing || m_seekTicks > 0)
    {
        const int64_t pos = currentMs();
        if (m_timeline != NULL)
            m_timeline->SetPosition(pos, playing);
        updateTimeText();
        updateCaptionOverlay();
        //播放时选中项跟着当前字幕走（编辑区有没应用的改动时不跟，免得打断输入）
        if (playing && !panelModified())
        {
            const int idx = m_model.Track().IndexAtTime(pos);
            if (idx >= 0 && idx != m_model.Selected())
            {
                m_model.Select(idx);
                refreshSelection();
            }
        }
    }
    if (m_playBtn != NULL && (m_player.IsPlaying() ? std::wstring(TR(L"暂停")) : std::wstring(TR(L"播放"))) !=
                                 std::wstring(m_playBtn->GetText().GetString()))
        updateToolbar();
    return 0;
}

LRESULT CaptionEditorWindow::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl)
    {
        switch (wParam)
        {
        //Ctrl+Z 撤销
        case 'Z':
            undo();
            return 0;
        //Ctrl+Y 重做
        case 'Y':
            redo();
            return 0;
        //Ctrl+S 立即保存
        case 'S':
            if (panelModified())
                applyEdits();
            saveNow();
            return 0;
        //Ctrl+空格 播放 / 暂停（单独的空格要留给文字输入）
        case VK_SPACE:
            togglePlay();
            return 0;
        //Ctrl+回车 应用编辑区
        case VK_RETURN:
            applyEdits();
            return 0;
        //其它组合键交给控件
        default:
            break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CaptionEditorWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    CloseEditor();
    return 0;
}

} // namespace caption
