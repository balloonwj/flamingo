/**
 *  字幕编辑器的导出窗口，见 CaptionExportDialog.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionExportDialog.h"

#include <shellapi.h>

#include "CaptionFormDialog.h"   //RunCaptionModalLoop
#include "../Host/CaptionModule.h"
#include "../Model/CaptionFiles.h"
#include "../Model/CaptionSettings.h"
#include "../../Source/Common/I18n/I18n.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

#pragma comment(lib, "shell32.lib")

namespace caption {

namespace {

//尺寸（逻辑像素，整窗含标题栏）；14.13 加了「纯文本」一行，高度由 330 加到 330 + kRowH + kGap
const int kDialogW = 520;
const int kDialogH = 368;
const int kTitleBarH = 32;
const int kPadding = 16;
const int kGap = 10;
const int kRowH = 28;
const int kNoteH = 36;
const int kProgressH = 16;
const int kStatusH = 40;
const int kButtonW = 112;
const int kButtonRowH = 30;
//进度条的满分（千分比）
const int kPermilleFull = 1000;
//控件 id（避开标题栏按钮占用的 1~3）
const UINT kIdHard = 10;
const UINT kIdTrack = 11;
const UINT kIdSrt = 12;
const UINT kIdStart = 13;
const UINT kIdFolder = 14;
const UINT kIdSend = 15;
const UINT kIdClose = 16;
const UINT kIdText = 17;
//说明文字颜色
const COLORREF kNoteColor = RGB(110, 114, 122);
//三种文件的文件名后缀与扩展名
const wchar_t* const kHardSuffix = TR_NOOP(L"-字幕");
const wchar_t* const kTrackSuffix = TR_NOOP(L"-字幕轨");
const wchar_t* const kMp4Ext = L".mp4";
const wchar_t* const kSrtExt = L".srt";
//纯文本的扩展名（14.13）
const wchar_t* const kTxtExt = L".txt";
//窗口标题
const wchar_t* const kTitle = TR_NOOP(L"导出字幕");

bool FileExistsAt(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

//种类的显示名
std::wstring KindName(int kind)
{
    switch (kind)
    {
    //硬字幕版
    case caption_export_hard:
        return TR(L"硬字幕版");
    //字幕轨版
    case caption_export_track:
        return TR(L"字幕轨版");
    //纯文本（14.13）
    case caption_export_text:
        return TR(L"纯文本");
    //外挂字幕
    default:
        return TR(L"SRT 字幕文件");
    }
}

} // namespace

CaptionExportDialog::CaptionExportDialog()
    : m_finished(false)
    , m_closeAfter(false)
    , m_modalDone(false)
    , m_audioOnly(false)
    , m_hardCheck(NULL)
    , m_trackCheck(NULL)
    , m_srtCheck(NULL)
    , m_textCheck(NULL)
    , m_progress(NULL)
    , m_status(NULL)
    , m_startBtn(NULL)
    , m_folderBtn(NULL)
    , m_sendBtn(NULL)
    , m_closeBtn(NULL)
{
}

CaptionExportDialog::~CaptionExportDialog()
{
    m_job.Cancel();
    m_job.Wait();
    if (IsWindow())
        DestroyWindow();
}

void CaptionExportDialog::Run(HWND owner, const CaptionDraft& draft, const std::wstring& videoPath,
                              std::vector<CaptionExportItemResult>& done)
{
    m_draft = draft;
    m_videoPath = videoPath;
    m_modalDone = false;
    //录音（14.13）没有画面：只提供 SRT 与纯文本
    m_audioOnly = IsAudioOnlyMediaPath(videoPath);
    //有所有者的弹窗用 WS_OVERLAPPEDWINDOW，不能用 WS_POPUP：balloonui 按 GetParent 决定控件通知发给谁，有所有者的
    //WS_POPUP 窗口 GetParent 返回所有者，按钮的点击通知（含标题栏关闭按钮）会全部发到编辑器窗口，本窗口一条都收不到
    //（2026-10-06 导出窗口点「开始导出」没反应、也关不掉，即由此而来；外观由 DuiFrameWindow 自绘，不受影响）
    if (Create(owner, CWindow::rcDefault, TR(kTitle), WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME) == NULL)
        return;
    SetTitle(TR(kTitle));
    SetTitleBarHeight(kTitleBarH);
    //标题栏显示程序图标（开发约定文档「UI 约定」第 10 条）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetButtons(false, false, true);
    SetResizable(false);
    ResizeClient(kDialogW, kDialogH);
    buildUi();
    RECT ownerRc = { 0, 0, 0, 0 };
    if (owner != NULL && ::GetWindowRect(owner, &ownerRc))
    {
        const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - kDialogW) / 2;
        const int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - kDialogH) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RunCaptionModalLoop(m_hWnd, owner, m_modalDone);
    //模态循环结束时导出线程一定已结束（导出中关闭会先取消并等它结束）
    m_job.Wait();
    done = m_done;
}

void CaptionExportDialog::buildUi()
{
    const CaptionSettings settings = CCaptionModule::Instance().GetSettings();
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> note(new balloonwjui::DuiLabel());
    note->SetText(m_audioOnly ? TR(L"导出的文件与录音放在同一个文件夹，原录音保留。录音没有画面，只能导出 SRT 与纯文本。")
                              : TR(L"导出的文件与录像放在同一个文件夹，原录像保留。字幕轨版需要播放器支持内嵌字幕。"));
    note->SetTextColor(kNoteColor);
    note->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
    root->AddChild(std::move(note), balloonwjui::DuiLayout::Hint().Fixed(kNoteH));
    //勾选项：文字里带上目标文件名；录音（14.13）没有硬字幕版与字幕轨版
    struct CheckDef
    {
        UINT                     m_id;
        std::wstring             m_text;
        bool                     m_on;
        balloonwjui::DuiButton** m_out;
    };
    //录音的默认勾选沿用上次的 SRT 与纯文本；两项都没勾过时默认勾 SRT，免得「开始导出」一打开就不可用
    bool srtOn = settings.m_exportSrt;
    const bool textOn = settings.m_exportText;
    if (m_audioOnly && !srtOn && !textOn)
        srtOn = true;
    std::vector<CheckDef> defs;
    if (!m_audioOnly)
    {
        const CheckDef hard = { kIdHard, TRF(L"硬字幕版（字幕画进画面）：{0}",
                                             FileNameOf(MakeExportPath(m_videoPath, TR(kHardSuffix), kMp4Ext))),
                                settings.m_exportHard, &m_hardCheck };
        const CheckDef track = { kIdTrack, TRF(L"字幕轨版（可开关的字幕轨）：{0}",
                                               FileNameOf(MakeExportPath(m_videoPath, TR(kTrackSuffix), kMp4Ext))),
                                 settings.m_exportTrack, &m_trackCheck };
        defs.push_back(hard);
        defs.push_back(track);
    }
    const CheckDef srt = { kIdSrt, TRF(L"SRT 字幕文件：{0}", FileNameOf(MakeExportPath(m_videoPath, L"", kSrtExt))),
                           srtOn, &m_srtCheck };
    const CheckDef text = { kIdText, TRF(L"纯文本（每句一行，带时间）：{0}",
                                         FileNameOf(MakeExportPath(m_videoPath, L"", kTxtExt))),
                            textOn, &m_textCheck };
    defs.push_back(srt);
    defs.push_back(text);
    for (size_t i = 0; i < defs.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiButton> check(new balloonwjui::DuiButton());
        check->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        check->SetText(defs[i].m_text.c_str());
        check->SetCheck(defs[i].m_on, false);
        check->SetCtrlId(defs[i].m_id);
        *defs[i].m_out = check.get();
        root->AddChild(std::move(check), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }
    std::unique_ptr<balloonwjui::DuiProgressBar> progress(new balloonwjui::DuiProgressBar());
    progress->SetRange(0, kPermilleFull);
    progress->SetPos(0, false);
    progress->SetVisible(false);
    m_progress = progress.get();
    root->AddChild(std::move(progress), balloonwjui::DuiLayout::Hint().Fixed(kProgressH));
    std::unique_ptr<balloonwjui::DuiLabel> status(new balloonwjui::DuiLabel());
    status->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
    m_status = status.get();
    root->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Fixed(kStatusH));
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->SetGap(kGap);
    struct ButtonDef
    {
        UINT                     m_id;
        const wchar_t*           m_text;
        bool                     m_primary;
        balloonwjui::DuiButton** m_out;
    };
    const ButtonDef bdefs[] = {
        { kIdFolder, TR(L"打开所在文件夹"), false, &m_folderBtn },
        { kIdSend, TR(L"发送到会话…"), false, &m_sendBtn },
        { kIdStart, TR(L"开始导出"), true, &m_startBtn },
        { kIdClose, TR(L"关闭"), false, &m_closeBtn },
    };
    for (size_t i = 0; i < sizeof(bdefs) / sizeof(bdefs[0]); ++i)
    {
        if (i == 2)
            buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                              balloonwjui::DuiLayout::Hint().Weight(1));
        std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
        b->SetText(bdefs[i].m_text);
        b->SetVariant(bdefs[i].m_primary ? balloonwjui::DuiButton::Variant::Primary
                                         : balloonwjui::DuiButton::Variant::Default);
        b->SetCtrlId(bdefs[i].m_id);
        *bdefs[i].m_out = b.get();
        buttons->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    }
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    SetClientContent(std::move(root));
    updateButtons();
}

void CaptionExportDialog::updateButtons()
{
    const bool running = m_job.IsRunning();
    const bool any = (m_hardCheck != NULL && m_hardCheck->IsChecked()) ||
                     (m_trackCheck != NULL && m_trackCheck->IsChecked()) ||
                     (m_srtCheck != NULL && m_srtCheck->IsChecked()) ||
                     (m_textCheck != NULL && m_textCheck->IsChecked());
    if (m_startBtn != NULL)
        m_startBtn->SetEnabled(!running && any);
    if (m_hardCheck != NULL)
        m_hardCheck->SetEnabled(!running);
    if (m_trackCheck != NULL)
        m_trackCheck->SetEnabled(!running);
    if (m_srtCheck != NULL)
        m_srtCheck->SetEnabled(!running);
    if (m_textCheck != NULL)
        m_textCheck->SetEnabled(!running);
    if (m_folderBtn != NULL)
        m_folderBtn->SetEnabled(!running && !m_done.empty());
    if (m_sendBtn != NULL)
        m_sendBtn->SetEnabled(!running && !m_done.empty());
    if (m_closeBtn != NULL)
        m_closeBtn->SetText(running ? TR(L"取消") : TR(L"关闭"));
}

void CaptionExportDialog::start()
{
    if (m_job.IsRunning())
        return;
    CaptionExportRequest req;
    req.m_videoPath = m_videoPath;
    req.m_cues = m_draft.m_cues;
    req.m_style = m_draft.m_style;
    req.m_pip = m_draft.m_pip;
    //录音（14.13）没有硬字幕版与字幕轨版两项
    req.m_hard = (m_hardCheck != NULL && m_hardCheck->IsChecked());
    req.m_track = (m_trackCheck != NULL && m_trackCheck->IsChecked());
    req.m_srt = (m_srtCheck != NULL && m_srtCheck->IsChecked());
    req.m_text = (m_textCheck != NULL && m_textCheck->IsChecked());
    req.m_hardPath = MakeExportPath(m_videoPath, TR(kHardSuffix), kMp4Ext);
    req.m_trackPath = MakeExportPath(m_videoPath, TR(kTrackSuffix), kMp4Ext);
    req.m_srtPath = MakeExportPath(m_videoPath, L"", kSrtExt);
    req.m_textPath = MakeExportPath(m_videoPath, L"", kTxtExt);
    //目标文件已存在：先问是否覆盖
    std::wstring existing;
    if (req.m_hard && FileExistsAt(req.m_hardPath))
        existing += L"\n" + FileNameOf(req.m_hardPath);
    if (req.m_track && FileExistsAt(req.m_trackPath))
        existing += L"\n" + FileNameOf(req.m_trackPath);
    if (req.m_srt && FileExistsAt(req.m_srtPath))
        existing += L"\n" + FileNameOf(req.m_srtPath);
    if (req.m_text && FileExistsAt(req.m_textPath))
        existing += L"\n" + FileNameOf(req.m_textPath);
    if (!existing.empty())
    {
        const std::wstring tip = TRF(L"以下文件已存在，要覆盖吗？{0}", existing);
        if (::MessageBoxW(m_hWnd, tip.c_str(), TR(kTitle), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    }
    //记住这次的勾选（D1）；录音没有硬字幕版与字幕轨版，这两项保留录像上次的选择
    CaptionSettings settings = CCaptionModule::Instance().GetSettings();
    const bool hard = m_audioOnly ? settings.m_exportHard : req.m_hard;
    const bool track = m_audioOnly ? settings.m_exportTrack : req.m_track;
    if (settings.m_exportHard != hard || settings.m_exportTrack != track || settings.m_exportSrt != req.m_srt ||
        settings.m_exportText != req.m_text)
    {
        settings.m_exportHard = hard;
        settings.m_exportTrack = track;
        settings.m_exportSrt = req.m_srt;
        settings.m_exportText = req.m_text;
        CCaptionModule::Instance().UpdateSettings(settings);
    }
    m_done.clear();
    if (!m_job.Start(req, this))
        return;
    LOG_INFO("caption editor: export started, hard=%d, track=%d, srt=%d, text=%d, audio=%d.", req.m_hard ? 1 : 0,
             req.m_track ? 1 : 0, req.m_srt ? 1 : 0, req.m_text ? 1 : 0, m_audioOnly ? 1 : 0);
    m_progress->SetPos(0, false);
    m_progress->SetVisible(true);
    m_status->SetText(TR(L"正在导出…"));
    //SetVisible 不会重排（开发约定文档 UI 约定第 5 条）
    balloonwjui::DuiControl* parent = m_progress->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    updateButtons();
}

void CaptionExportDialog::openFolder()
{
    if (m_done.empty())
        return;
    const std::wstring args = L"/select,\"" + m_done[0].m_path + L"\"";
    ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}

void CaptionExportDialog::sendToConversation()
{
    if (m_done.empty())
        return;
    //优先发硬字幕版，其次字幕轨版、SRT，最后纯文本
    const int order[] = { caption_export_hard, caption_export_track, caption_export_srt, caption_export_text };
    std::wstring path;
    for (size_t k = 0; k < sizeof(order) / sizeof(order[0]) && path.empty(); ++k)
    {
        for (size_t i = 0; i < m_done.size(); ++i)
        {
            if (m_done[i].m_kind == order[k])
            {
                path = m_done[i].m_path;
                break;
            }
        }
    }
    ICaptionHost* host = CCaptionModule::Instance().Host();
    if (host != NULL && !path.empty())
        host->PickConversationAndSendFile(path.c_str());
}

void CaptionExportDialog::finish()
{
    if (m_job.IsRunning())
    {
        //导出中：先取消，等后台线程结束（OnDoneMsg）再关闭
        m_closeAfter = true;
        m_job.Cancel();
        m_status->SetText(TR(L"正在取消…"));
        return;
    }
    m_modalDone = true;
    if (IsWindow())
        DestroyWindow();
}

void CaptionExportDialog::OnExportProgress(int kind, int permille, int overall)
{
    (void)permille;
    if (IsWindow())
        PostMessage(kMsgCaptionExportProgress, static_cast<WPARAM>(overall), static_cast<LPARAM>(kind));
}

void CaptionExportDialog::OnExportFinished(const CaptionExportResult& result)
{
    CaptionExportResult* copy = new CaptionExportResult(result);
    if (!IsWindow() || !PostMessage(kMsgCaptionExportDone, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

LRESULT CaptionExportDialog::OnProgressMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (m_progress != NULL)
        m_progress->SetPos(static_cast<int>(wParam), false);
    if (m_status != NULL && !m_closeAfter)
        m_status->SetText(TRF(L"正在生成{0}…", KindName(static_cast<int>(lParam))).c_str());
    return 0;
}

LRESULT CaptionExportDialog::OnDoneMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<CaptionExportResult> result(reinterpret_cast<CaptionExportResult*>(lParam));
    m_job.Wait();
    m_finished = true;
    std::wstring text;
    if (result)
    {
        for (size_t i = 0; i < result->m_items.size(); ++i)
        {
            const CaptionExportItemResult& item = result->m_items[i];
            if (item.m_ok)
                m_done.push_back(item);
            if (!text.empty())
                text += L"  ";
            text += item.m_ok ? TRF(L"{0}：已生成", KindName(item.m_kind)) : TRF(L"{0}：失败", KindName(item.m_kind));
        }
        if (result->m_cancelled)
            text += (text.empty() ? L"" : L"  ") + std::wstring(TR(L"导出已取消"));
    }
    if (m_status != NULL)
        m_status->SetText(text.c_str());
    if (m_progress != NULL && result && !result->m_cancelled)
        m_progress->SetPos(kPermilleFull, false);
    updateButtons();
    if (m_closeAfter)
    {
        m_closeAfter = false;
        finish();
    }
    return 0;
}

LRESULT CaptionExportDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //勾选变化：刷新「开始导出」是否可用
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) &&
        (n->ctrlId == kIdHard || n->ctrlId == kIdTrack || n->ctrlId == kIdSrt || n->ctrlId == kIdText))
    {
        updateButtons();
        return 0;
    }
    if (n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    //开始导出
    case kIdStart:
        start();
        return 0;
    //打开所在文件夹
    case kIdFolder:
        openFolder();
        return 0;
    //发送到会话
    case kIdSend:
        sendToConversation();
        return 0;
    //关闭（导出中为取消）
    case kIdClose:
        finish();
        return 0;
    //勾选项的点击：勾选状态由 DUIN_VALUECHANGED 处理，这里刷新按钮
    case kIdHard:
    case kIdTrack:
    case kIdSrt:
    case kIdText:
        updateButtons();
        return 0;
    //其它控件
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CaptionExportDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish();
    return 0;
}

} // namespace caption
