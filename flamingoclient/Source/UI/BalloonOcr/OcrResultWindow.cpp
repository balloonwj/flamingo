/**
 *  「图片文字识别」结果窗口，见 OcrResultWindow.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "OcrResultWindow.h"

#include <string.h>

#include "OcrResultLayout.h"
#include "../../Feature/Ocr/OcrModuleHost.h"
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：「打开图片」对话框的过滤串
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）

using namespace balloonwjui;

namespace balloonocr {

namespace {

// ---- 标题栏：透明、穿透显示客户区白底，与接龙窗口一致 ----
const COLORREF kCaptionColor  = RGB(0x55, 0x55, 0x55);
const int      kTitleIconSize = 16;
const COLORREF kTitleBarBg    = RGB(0xFF, 0xFF, 0xFF);

// 窗口标题
const wchar_t* const kWindowTitle = TR_NOOP(L"图片文字识别");

} // anonymous namespace

COcrResultWindow::COcrResultWindow()
    : m_taskId(0)
    , m_pLanguage(NULL)
    , m_pStatus(NULL)
    , m_pText(NULL)
    , m_pRetry(NULL)
    , m_pCopy(NULL)
    , m_pOpen(NULL)
    , m_hTitleIcon(NULL)
{
}

COcrResultWindow::~COcrResultWindow()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

COcrResultWindow& COcrResultWindow::GetInstance()
{
    //有意不释放：进程退出时窗口随进程一起销毁
    static COcrResultWindow* s_window = new COcrResultWindow();
    return *s_window;
}

bool COcrResultWindow::OpenForFile(HWND anchor, const std::wstring& path, int quarterTurns)
{
    if (path.empty())
        return false;
    ocr::OcrRequest request;
    request.m_filePath = path;
    request.m_quarterTurns = quarterTurns;
    return Open(anchor, request);
}

bool COcrResultWindow::OpenForImage(HWND anchor, const ocr::OcrBgraImage& image)
{
    if (image.m_width == 0 || image.m_height == 0 || image.m_pixels.empty())
        return false;
    ocr::OcrRequest request;
    request.m_image = image;
    return Open(anchor, request);
}

bool COcrResultWindow::Open(HWND anchor, const ocr::OcrRequest& request)
{
    const bool existed = (IsWindow() != FALSE);
    if (!EnsureWindow())
        return false;

    CancelRunning();
    m_request = request;
    if (m_pText != NULL)
        m_pText->SetText(_T(""));

    //新建的窗口居中摆在发起识别的窗口上；已打开的窗口保持用户放的位置
    if (!existed)
        CenterOnAnchor(anchor);
    ShowWindow(::IsIconic(m_hWnd) ? SW_RESTORE : SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    StartRecognition();
    if (request.m_filePath.empty())
        LOG_INFO("ocr result window opened: source=image %ux%u, reused=%d.", request.m_image.m_width,
                 request.m_image.m_height, existed ? 1 : 0);
    else
        LOG_INFO("ocr result window opened: source=file, rotation=%d, reused=%d.", request.m_quarterTurns, existed ? 1 : 0);
    return true;
}

bool COcrResultWindow::OpenEmpty(HWND anchor)
{
    const bool existed = (IsWindow() != FALSE);
    if (!EnsureWindow())
        return false;

    //已打开的窗口保留其中的图片与结果，只调到前台；新建的窗口提示用户选一张图片
    if (!existed)
    {
        m_request = ocr::OcrRequest();
        if (m_pText != NULL)
            m_pText->SetText(_T(""));
        SetStatus(OcrNoImageText());
        if (m_pRetry != NULL)
            m_pRetry->SetEnabled(false);
        CenterOnAnchor(anchor);
    }
    ShowWindow(::IsIconic(m_hWnd) ? SW_RESTORE : SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("ocr result window opened without an image, reused=%d.", existed ? 1 : 0);
    return true;
}

void COcrResultWindow::CenterOnAnchor(HWND anchor)
{
    if (anchor != NULL && ::IsWindow(anchor))
        CenterWindow(anchor);
    else
        CenterWindow(::GetDesktopWindow());
}

bool COcrResultWindow::EnsureWindow()
{
    if (IsWindow())
        return true;
    if (Create(NULL, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("ocr result window: Create failed.");
        return false;
    }
    SetTitle(TR(kWindowTitle));
    SetTitleBarHeight(kOcrResultTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    if (m_hTitleIcon == NULL)
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
    if (m_hTitleIcon != NULL)
        SetIcon(m_hTitleIcon);
    //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
    m_taskIcons.ApplyTaskIcons(m_hWnd);
    SetButtons(true, true, true);
    SetResizable(true);
    //最小尺寸是整窗尺寸（含标题栏），见开发约定文档 UI 约定第 7 条
    SetMinSize(kOcrResultMinWidth, kOcrResultMinHeight);
    BuildClient();
    FillLanguages();
    ResizeClient(kOcrResultWindowWidth, kOcrResultWindowHeight);
    return true;
}

void COcrResultWindow::BuildClient()
{
    SetClientContent(BuildOcrResultBody());
    DuiControl* root = GetClientContent();
    if (root == NULL)
        return;
    m_pLanguage = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kOcrIdLanguage));
    m_pStatus   = dynamic_cast<DuiLabel*>(root->FindCtrlById(kOcrIdStatus));
    m_pText     = dynamic_cast<DuiRichEdit*>(root->FindCtrlById(kOcrIdText));
    m_pRetry    = dynamic_cast<DuiButton*>(root->FindCtrlById(kOcrIdRetry));
    m_pCopy     = dynamic_cast<DuiButton*>(root->FindCtrlById(kOcrIdCopy));
    m_pOpen     = dynamic_cast<DuiButton*>(root->FindCtrlById(kOcrIdOpen));
}

void COcrResultWindow::FillLanguages()
{
    m_languageTags.clear();
    if (m_pLanguage == NULL)
        return;
    m_pLanguage->ResetContent();

    const ocr::OcrHostSnapshot snapshot = ocr::COcrModuleHost::GetInstance().GetSnapshot();
    int selected = -1;
    for (size_t i = 0; i < snapshot.m_languages.size(); ++i)
    {
        const ocr::OcrLanguageOption& option = snapshot.m_languages[i];
        //显示名由引擎按系统语言给出；取不到时显示代码本身
        const std::wstring label = option.m_name.empty() ? option.m_tag : option.m_name;
        m_pLanguage->AddString(label.c_str());
        m_languageTags.push_back(option.m_tag);
        if (option.m_tag == snapshot.m_defaultLanguage)
            selected = static_cast<int>(i);
    }
    if (selected < 0 && !m_languageTags.empty())
        selected = 0;
    if (selected >= 0)
        m_pLanguage->SetCurSel(selected, false);
}

std::wstring COcrResultWindow::SelectedLanguage() const
{
    if (m_pLanguage == NULL)
        return std::wstring();
    const int index = m_pLanguage->GetCurSel();
    if (index < 0 || static_cast<size_t>(index) >= m_languageTags.size())
        return std::wstring();
    return m_languageTags[index];
}

void COcrResultWindow::StartRecognition()
{
    CancelRunning();
    //还没有图片（系统菜单打开的空窗口）：提示用户先打开一张图片
    if (!ocr::HasImageSource(m_request))
    {
        SetStatus(OcrNoImageText());
        if (m_pRetry != NULL)
            m_pRetry->SetEnabled(false);
        return;
    }
    ocr::OcrRequest request = m_request;
    request.m_languageTag = SelectedLanguage();
    request.m_target = m_hWnd;
    request.m_message = kMsgOcrRecognized;
    m_taskId = ocr::COcrRecognizer::GetInstance().Submit(request);
    if (m_taskId == 0)
    {
        SetStatus(OcrResultStatusText(ocr::ocr_task_no_engine, 0));
        return;
    }
    SetStatus(OcrRecognizingText());
    if (m_pRetry != NULL)
        m_pRetry->SetEnabled(false);
}

void COcrResultWindow::CancelRunning()
{
    if (m_taskId != 0)
    {
        ocr::COcrRecognizer::GetInstance().Cancel(m_taskId);
        m_taskId = 0;
    }
    if (m_pRetry != NULL)
        m_pRetry->SetEnabled(ocr::HasImageSource(m_request));
}

void COcrResultWindow::ChooseImageFile()
{
    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"图片文件"), filedlg::kImagePatterns, TR(L"所有文件"), L"*.*");
    CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER, fileFilter.c_str(), m_hWnd);
    if (dlg.DoModal() != IDOK)
        return;

    ocr::OcrRequest request;
    request.m_filePath = dlg.m_szFileName;
    m_request = request;
    if (m_pText != NULL)
        m_pText->SetText(_T(""));
    LOG_INFO("ocr result window: image chosen from a file dialog.");
    StartRecognition();
}

void COcrResultWindow::SetStatus(const std::wstring& text)
{
    if (m_pStatus != NULL)
        m_pStatus->SetText(text.c_str());
}

void COcrResultWindow::CopyAll()
{
    if (m_pText == NULL)
        return;
    const std::wstring text = NormalizeLineBreaks(std::wstring((LPCWSTR)m_pText->GetText()));
    if (text.empty())
    {
        SetStatus(TR(L"没有可复制的文字"));
        return;
    }
    if (!::OpenClipboard(m_hWnd))
    {
        LOG_WARNING("ocr result window: OpenClipboard failed, error %u.", static_cast<unsigned>(::GetLastError()));
        SetStatus(TR(L"复制失败，剪贴板被其它程序占用"));
        return;
    }
    ::EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool copied = false;
    if (memory != NULL)
    {
        void* p = ::GlobalLock(memory);
        if (p != NULL)
        {
            memcpy(p, text.c_str(), bytes);
            ::GlobalUnlock(memory);
            //交给剪贴板之后所有权即归系统，不能再 GlobalFree
            copied = (::SetClipboardData(CF_UNICODETEXT, memory) != NULL);
            if (!copied)
                ::GlobalFree(memory);
        }
        else
        {
            ::GlobalFree(memory);
        }
    }
    ::CloseClipboard();
    SetStatus(copied ? TR(L"已复制到剪贴板") : TR(L"复制失败，剪贴板被其它程序占用"));
    LOG_INFO("ocr result window: copied %u chars, ok=%d.", static_cast<unsigned>(text.size()), copied ? 1 : 0);
}

LRESULT COcrResultWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //换了识别语言：按新语言重新识别（填下拉框时用 SetCurSel(..., false)，不会走到这里）
    if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == kOcrIdLanguage)
    {
        StartRecognition();
        return 0;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「重新识别」：按当前语言再识别一次
        case kOcrIdRetry:
            StartRecognition();
            return 0;

        //「复制全部」：复制文字框里当前的内容（用户可能改过）
        case kOcrIdCopy:
            CopyAll();
            return 0;

        //「打开图片」：选一张本地图片，换成它并开始识别
        case kOcrIdOpen:
            ChooseImageFile();
            return 0;

        //其它控件的点击（含标题栏按钮）：交给框架
        default:
            break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT COcrResultWindow::OnRecognized(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    ocr::OcrTextResult* result = reinterpret_cast<ocr::OcrTextResult*>(lParam);
    if (result == NULL)
        return 0;
    //换了语言或换了图片之后，上一个任务迟到的结果直接丢弃
    if (result->m_taskId != m_taskId)
    {
        delete result;
        return 0;
    }
    m_taskId = 0;
    if (m_pRetry != NULL)
        m_pRetry->SetEnabled(true);
    if (result->m_code == flamingoocr::ocr_ok && m_pText != NULL)
        m_pText->SetText(result->JoinedText().c_str());
    SetStatus(OcrResultStatusText(result->m_code, result->m_lines.size()));
    delete result;
    return 0;
}

LRESULT COcrResultWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    CancelRunning();
    if (IsWindow())
        DestroyWindow();
    return 0;
}

LRESULT COcrResultWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    CancelRunning();
    //控件树随窗口销毁，借用的指针一并作废
    m_pLanguage = NULL;
    m_pStatus = NULL;
    m_pText = NULL;
    m_pRetry = NULL;
    m_pCopy = NULL;
    m_pOpen = NULL;
    m_languageTags.clear();
    bHandled = FALSE;
    return 0;
}

} // namespace balloonocr
