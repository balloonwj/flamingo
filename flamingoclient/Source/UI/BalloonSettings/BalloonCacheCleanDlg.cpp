/**
 *  「清理本地缓存」对话框实现，BalloonCacheCleanDlg.cpp
 *  设计与职责分工见 BalloonCacheCleanDlg.h。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"
#include "BalloonCacheCleanDlg.h"

#include "CacheCleanExecutor.h"
#include "../../App/FlamingoClient.h"
#include "../../Session/UserSessionData.h"      // g_szDataRoot
#include "../../Common/Log/IULog.h"

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace {

// ---- 控件 id ----
// 重要：DuiFrameWindow 内部 min/max/close 标题按钮固定占用 1/2/3，客户区控件不能用
// （与系统设置窗同一条约束）。
const UINT kIdSwitchBase = 10;   // 第 i 类的开关 id = 基址 + i
const UINT kIdBtnClean   = 20;   // 「开始清理」
const UINT kIdBtnCancel  = 21;   // 「取消」

// ---- 尺寸（像素）----
const int kDlgWidth    = 460;   // 窗口宽：够放"名称 + 占用量 + 开关"一行
const int kDlgHeight   = 320;   // 内容区高（标题栏以下）：说明 + 三类 + 合计 + 按钮行
const int kTitleBarH   = 38;    // 标题栏高
const int kPadH        = 20;    // 客户区左右内边距
const int kPadV        = 16;    // 客户区上下内边距
const int kTipH        = 40;    // 顶部说明文字高（两行）
const int kRowH        = 44;    // 每一类一行的高度
const int kRowGap      = 12;    // 行内各块的水平间距
const int kSizeLabelW  = 90;    // 占用量文本列宽
const int kSwitchW     = 44;    // 开关宽
const int kSwitchH     = 24;    // 开关高
const int kTotalH      = 28;    // 合计行高
const int kFooterH     = 52;    // 底部按钮行高
const int kBtnW        = 96;    // 按钮宽
const int kBtnH        = 32;    // 按钮高
const int kBtnGap      = 10;    // 两个按钮之间的间距

// ---- 配色 ----
const COLORREF kColorWhite   = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorTitle   = RGB(0x22, 0x22, 0x22);
const COLORREF kColorSub     = RGB(0x9A, 0x98, 0x93);
const COLORREF kColorSepLine = RGB(0xEC, 0xEA, 0xE6);
const COLORREF kAccentOrange = RGB(0xF2, 0x6B, 0x3E);   // 开关开启色，与系统设置窗一致

// 开关滑动动画的中间帧由 DuiAnimMgr 推进，本对话框不必为此挂定时器：DuiAnimMgr
// 自 2026-08-14 起自带脉冲定时器（见 balloonui/DuiAnimation.cpp 的 StartPulse /
// PulseProc），有动画在跑时它自己按 16ms 发 WM_TIMER，跑完即停。

// 造一个文本标签。
std::unique_ptr<balloonwjui::DuiControl> MakeLabel(LPCTSTR text, COLORREF color, DWORD align)
{
    std::unique_ptr<balloonwjui::DuiLabel> lbl(new balloonwjui::DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(align);
    return std::unique_ptr<balloonwjui::DuiControl>(lbl.release());
}

} // anonymous namespace

CBalloonCacheCleanDlg::CBalloonCacheCleanDlg(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_ownerHwnd(NULL)
{
}

CBalloonCacheCleanDlg::~CBalloonCacheCleanDlg()
{
}

void CBalloonCacheCleanDlg::OpenDialog(HWND owner)
{
    m_ownerHwnd = owner;

    if (IsWindow())
    {
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        return;
    }

    // 先算计划、再扫占用量：两者都要在建树之前完成，行上要显示占用量。
    const std::wstring account = (m_lpFMGClient != NULL)
                               ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount
                               : std::wstring();
    m_plan = balloonsettings::BuildCacheCleanPlan(g_szDataRoot, account);
    if (m_plan.empty())
    {
        // 还没登录时没有可清的东西，直接提示，不弹一个空窗让用户对着发愣。
        ::MessageBox(owner, TR(L"尚未登录，没有可清理的缓存。"),
                     TR(L"清理本地缓存"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    ScanSizes();

    if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("清理缓存对话框 Create 失败。"));
        return;
    }

    SetTitle(TR(L"清理本地缓存"));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);   // 只留关闭按钮
    SetResizable(false);
    // 标题栏左侧的程序图标：标题栏不透明，底色用 kOpaqueTitleBarColor；本窗口没有所有者、会出现在任务栏，同时设好任务栏图标
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);

    InitUi();

    //kDlgHeight 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
    ResizeClient(kDlgWidth, kDlgHeight + kTitleBarH);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
}

void CBalloonCacheCleanDlg::ScanSizes()
{
    m_sizes.clear();
    m_sizes.reserve(m_plan.size());

    for (size_t i = 0; i < m_plan.size(); ++i)
    {
        unsigned long long bytes = 0;
        for (size_t k = 0; k < m_plan[i].m_dirs.size(); ++k)
        {
            bytes += balloonsettings::ScanDirSizeBytes(m_plan[i].m_dirs[k]);
        }
        m_sizes.push_back(bytes);
    }
}

void CBalloonCacheCleanDlg::InitUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadV, kPadH, kPadV);
    root->SetGap(0);

    // 顶部说明：讲清"清的是什么、不会动什么"。这是用户点下"开始清理"前唯一的依据。
    root->AddChild(MakeLabel(TR(L"清理后这些内容需要时会重新下载。聊天记录、公告与本地设置不受影响。"),
                             kColorSub, DT_LEFT | DT_TOP | DT_WORDBREAK),
                   balloonwjui::DuiLayout::Hint().Fixed(kTipH));

    unsigned long long total = 0;
    for (size_t i = 0; i < m_plan.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);

        row->AddChild(MakeLabel(m_plan[i].m_title, kColorTitle,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                      balloonwjui::DuiLayout::Hint().Weight(1));

        const unsigned long long bytes = (i < m_sizes.size()) ? m_sizes[i] : 0;
        total += bytes;
        row->AddChild(MakeLabel(CString(balloonsettings::FormatByteSize(bytes).c_str()),
                                kColorSub, DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                      balloonwjui::DuiLayout::Hint().Fixed(kSizeLabelW));

        std::unique_ptr<balloonwjui::DuiSwitch> sw(new balloonwjui::DuiSwitch());
        sw->SetCtrlId(kIdSwitchBase + (UINT)i);
        sw->SetOnColor(kAccentOrange);
        // 默认全开：用户点开这个窗口本就是要清，逐个去开一遍是多余的操作。
        sw->SetChecked(true, /*animated*/false, /*notify*/false);
        row->AddChild(std::move(sw),
                      balloonwjui::DuiLayout::Hint().Fixed(kSwitchW, kSwitchH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));

        std::unique_ptr<balloonwjui::DuiSeparator> sep(new balloonwjui::DuiSeparator());
        sep->SetOrientation(balloonwjui::DuiSeparator::Horizontal);
        sep->SetColor(kColorSepLine);
        root->AddChild(std::move(sep), balloonwjui::DuiLayout::Hint().Fixed(1));
    }

    // 合计行：把三类加起来，用户一眼知道能腾出多少。
    CString totalText;
    totalText.Format(TR(L"合计 %s"), balloonsettings::FormatByteSize(total).c_str());
    root->AddChild(MakeLabel(totalText, kColorTitle, DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                   balloonwjui::DuiLayout::Hint().Fixed(kTotalH));

    // 中间弹性空白，把按钮行压到底部。
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));

    // 底部按钮行：右对齐的「取消」「开始清理」。
    std::unique_ptr<balloonwjui::DuiHBox> footer(new balloonwjui::DuiHBox());
    footer->SetGap(kBtnGap);
    footer->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                     balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    cancel->SetText(TR(L"取消"));
    cancel->SetCtrlId(kIdBtnCancel);
    footer->AddChild(std::move(cancel),
                     balloonwjui::DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                         .AlignC(balloonwjui::DuiLayout::AlignCenter));

    std::unique_ptr<balloonwjui::DuiButton> clean(new balloonwjui::DuiButton());
    clean->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    clean->SetText(TR(L"开始清理"));
    clean->SetCtrlId(kIdBtnClean);
    footer->AddChild(std::move(clean),
                     balloonwjui::DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                         .AlignC(balloonwjui::DuiLayout::AlignCenter));

    root->AddChild(std::move(footer), balloonwjui::DuiLayout::Hint().Fixed(kFooterH));

    SetClientContent(std::move(root));
}

void CBalloonCacheCleanDlg::DoClean()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    unsigned long long freed = 0;
    int cleanedCount = 0;
    bool allOk = true;

    for (size_t i = 0; i < m_plan.size(); ++i)
    {
        balloonwjui::DuiSwitch* sw = dynamic_cast<balloonwjui::DuiSwitch*>(
            content->FindCtrlById(kIdSwitchBase + (UINT)i));
        if (sw == NULL || !sw->IsChecked())
        {
            continue;
        }

        ++cleanedCount;
        for (size_t k = 0; k < m_plan[i].m_dirs.size(); ++k)
        {
            if (!balloonsettings::DeleteDirTree(m_plan[i].m_dirs[k], g_szDataRoot))
            {
                allOk = false;
            }
        }
        freed += (i < m_sizes.size()) ? m_sizes[i] : 0;
    }

    if (cleanedCount == 0)
    {
        ::MessageBox(m_hWnd, TR(L"没有勾选任何要清理的内容。"),
                     TR(L"清理本地缓存"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    LOG_INFO(_T("清理缓存：已清理 %d 类，释放约 %s，是否全部删除成功=%d。"),
             cleanedCount, balloonsettings::FormatByteSize(freed).c_str(), (int)allOk);

    CString result;
    if (allOk)
    {
        result.Format(TR(L"清理完成，释放约 %s。"),
                      balloonsettings::FormatByteSize(freed).c_str());
    }
    else
    {
        // 有文件没删掉时如实说明，并给出最常见的原因，免得用户反复点。
        result.Format(TR(L"清理完成，释放约 %s。\n\n部分文件正在使用中，未能删除，"
                      L"可在传输结束后再清理一次。"),
                      balloonsettings::FormatByteSize(freed).c_str());
    }
    ::MessageBox(m_hWnd, result, TR(L"清理本地缓存"), MB_OK | MB_ICONINFORMATION);

    DestroyWindow();
}

LRESULT CBalloonCacheCleanDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    DestroyWindow();
    return 0;
}

LRESULT CBalloonCacheCleanDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    balloonwjui::DuiNotify* pNotify = (balloonwjui::DuiNotify*)lParam;
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    switch (pNotify->ctrlId)
    {
    //「开始清理」：按各行开关的状态执行删除，完成后提示结果并关窗。
    case kIdBtnClean:
        DoClean();
        return 0;

    //「取消」：什么都不做，直接关窗。
    case kIdBtnCancel:
        DestroyWindow();
        return 0;

    //其余（标题栏关闭按钮、各行开关自身的通知）交回基类。
    default:
        bHandled = FALSE;
        return 0;
    }
}
