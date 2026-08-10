/**
 *  flamingoAdmin —— 通用确认 / 信息对话框 ConfirmDialog 实现。
 *
 *  balloonwj@qq.com   2026-05-28
 */
#include "stdafx.h"
#include "Screens/ConfirmDialog.h"
#include "AdminFrame.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"          // UiFont / CopyTextToClipboard
#include "Widgets/Widgets.h"            // SolidHBox / IconView

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Basic/DuiButton.h"
#include "DuiNotify.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ===========================================================================
// 布局常量(96-dpi 逻辑像素;沿用上一代 ConfirmModal 的视觉规格,只把外层
// 自绘卡片换成 balloonui 自带 DuiFrameWindow 标题栏 + 客户区容器)
// ===========================================================================
const int kDlgClientWidth   = 420;  // 客户区宽度
const int kDlgClientHeight  = 174;  // 客户区高度(标题栏外)
const int kTitleBarHeight   = 36;   // 自绘标题栏高度

const int kBodyPad          = 20;   // 客户区四周内距
const int kBodyGap          = 14;   // 图标 与 消息文字之间间距
const int kIconBoxSize      = 40;   // 提示图标方块边长
const int kIconSize         = 22;   // 图标本体边长(居中)

const int kBtnBarH          = 50;   // 底部按钮栏高度
const int kBtnH             = 30;   // 按钮控件高度
const int kBtnW             = 84;   // 按钮控件宽度(取消 / 确认)
const int kBtnWCopy         = 96;   // "复制密码"宽一些(4 字)
const int kBtnGap           = 10;   // 按钮之间间距

const int kFontSizeNormal   = theme::kFontSizeNormal;

// ---- 控件 ID(本对话框 3 个按钮共用上一代 UiIds.h 里的 kIdConfirm* 段) ----
//   kIdConfirmCancel  取消
//   kIdConfirmCopy    复制密码
//   kIdConfirmOk      确认 / 知道了

// 造一个 DuiLabel。
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, int pt, bool bold,
                                    COLORREF color, bool wordWrap)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetFont(ui::UiFont(pt, bold));
    lbl->SetTextAlign(DT_LEFT | (wordWrap ? DT_TOP : DT_VCENTER)
                               | (wordWrap ? DT_WORDBREAK : DT_SINGLELINE)
                               | DT_NOPREFIX);
    if (wordWrap)
    {
        lbl->SetWordWrap(true);
    }
    return lbl;
}

// 造一个按钮。
std::unique_ptr<DuiButton> MakeButton(int ctrlId, LPCTSTR text,
                                       DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetCtrlId(ctrlId);
    btn->SetText(text);
    btn->SetVariant(variant);
    btn->SetTextPointSize(kFontSizeNormal);
    return btn;
}

// 造一个「图标方块」:固定 40×40,SolidHBox 上铺浅色背景,内嵌 IconView 居中。
std::unique_ptr<DuiControl> MakeIconBox(bool infoMode)
{
    // 浅红 / 浅绿背景,语义 = 危险 / 成功(与上一代 ConfirmModal 视觉一致)。
    const COLORREF kBgInfo   = RGB(0xE7, 0xF4, 0xEC);   // 浅绿
    const COLORREF kBgDanger = RGB(0xFF, 0xF0, 0xEF);   // 浅红

    std::unique_ptr<ui::SolidHBox> box(new ui::SolidHBox());
    box->SetFillColor(infoMode ? kBgInfo : kBgDanger);

    std::unique_ptr<ui::IconView> icon(new ui::IconView());
    icon->SetIcon(ui::Icon::Info);
    icon->SetColor(infoMode ? theme::kColorOk : theme::kColorDanger);
    icon->SetIconSize(kIconSize);

    // 图标 22×22 居中于 40×40 容器 —— 上下左右各留 9px margin。
    const int kIconMargin = (kIconBoxSize - kIconSize) / 2;
    box->AddChild(std::move(icon),
                  DuiLayout::Hint().Margin(kIconMargin).Weight(1));
    return std::unique_ptr<DuiControl>(box.release());
}

} // anonymous namespace

// ===========================================================================
// ConfirmDialog 公开接口
// ===========================================================================

ConfirmDialog::ConfirmDialog(AdminFrame* frame, HWND parentHwnd,
                              const ConfirmDialogSpec& spec)
    : m_frame(frame)
    , m_parentHwnd(parentHwnd)
    , m_spec(spec)
{
}

ConfirmDialog::~ConfirmDialog() = default;

bool ConfirmDialog::CreateAndShow()
{
    HWND hw = Create(NULL, CWindow::rcDefault, _T("FlamingoIM Admin"),
                     WS_OVERLAPPEDWINDOW, 0);
    if (hw == NULL || !::IsWindow(hw))
    {
        return false;
    }

    SetTitle(m_spec.m_title);
    SetTitleBarHeight(kTitleBarHeight);
    SetButtons(false, false, true);   // 仅保留关闭按钮
    SetResizable(false);

    BuildClientContent();

    ResizeClient(kDlgClientWidth, kDlgClientHeight + kTitleBarHeight);
    CenterWindow();

    if (m_parentHwnd != NULL && ::IsWindow(m_parentHwnd))
    {
        ::EnableWindow(m_parentHwnd, FALSE);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void ConfirmDialog::RequestClose()
{
    if (::IsWindow(m_hWnd))
    {
        ::PostMessage(m_hWnd, WM_CLOSE, 0, 0);
    }
}

// ===========================================================================
// ConfirmDialog 私有实现
// ===========================================================================

void ConfirmDialog::BuildClientContent()
{
    // ---- 根 vbox:padding 20,gap 0(主体与按钮栏之间无间距,bar 自带顶边线) ----
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(kBodyPad, kBodyPad, kBodyPad, 0);   // 底部 padding 留给按钮栏
    root->SetGap(0);

    // ---- 主体:左侧 40×40 图标 + 右侧消息文字(自动换行) ----
    {
        std::unique_ptr<DuiHBox> body(new DuiHBox());
        body->SetGap(kBodyGap);
        body->AddChild(MakeIconBox(m_spec.m_infoMode),
                       DuiLayout::Hint().Fixed(kIconBoxSize, kIconBoxSize)
                                         .AlignC(DuiLayout::AlignNear));
        body->AddChild(MakeLabel(m_spec.m_message, kFontSizeNormal, false,
                                  theme::kInk500, true),
                       DuiLayout::Hint().Weight(1));
        root->AddChild(std::unique_ptr<DuiControl>(body.release()),
                       DuiLayout::Hint().Weight(1));
    }

    // ---- 按钮栏:上边线 + 右对齐 ----
    {
        std::unique_ptr<ui::SolidHBox> bar(new ui::SolidHBox());
        bar->SetEdgeBorder(ui::SolidHBox::kEdgeTop, theme::kInk100);
        bar->SetPadding(0, 0, 0, 0);
        bar->SetGap(kBtnGap);

        // 弹性 spacer 把按钮顶到右
        bar->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                      DuiLayout::Hint().Weight(1));

        if (m_spec.m_infoMode)
        {
            // 信息模式:可选「复制密码」+ 确认。
            if (!m_spec.m_copyText.IsEmpty())
            {
                bar->AddChild(MakeButton(kIdConfirmCopy, _T("复制密码"),
                                          DuiButton::Variant::Default),
                              DuiLayout::Hint().Fixed(kBtnWCopy, kBtnH)
                                                .AlignC(DuiLayout::AlignCenter));
            }
            bar->AddChild(MakeButton(kIdConfirmOk, m_spec.m_okText,
                                      DuiButton::Variant::Primary),
                          DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                            .AlignC(DuiLayout::AlignCenter));
        }
        else
        {
            // 确认模式:取消 + 确认(danger 红 / primary 蓝按 spec)。
            bar->AddChild(MakeButton(kIdConfirmCancel, _T("取消"),
                                      DuiButton::Variant::Default),
                          DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                            .AlignC(DuiLayout::AlignCenter));
            bar->AddChild(MakeButton(kIdConfirmOk, m_spec.m_okText,
                                      m_spec.m_confirmDanger
                                          ? DuiButton::Variant::Danger
                                          : DuiButton::Variant::Primary),
                          DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                            .AlignC(DuiLayout::AlignCenter));
        }

        root->AddChild(std::unique_ptr<DuiControl>(bar.release()),
                       DuiLayout::Hint().Fixed(kBtnBarH));
    }

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
}

void ConfirmDialog::OnCopyButtonClicked()
{
    if (m_spec.m_copyText.IsEmpty())
    {
        return;
    }
    // 把 m_copyText 拷入系统剪贴板。等价于 AdminFrame.cpp 匿名 namespace
    // 里的 CopyTextToClipboard,在此 inline 一份避免跨 TU 拖出匿名符号。
    if (::OpenClipboard(m_hWnd))
    {
        ::EmptyClipboard();
        const size_t bytes =
            (m_spec.m_copyText.GetLength() + 1) * sizeof(TCHAR);
        HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hg != NULL)
        {
            void* p = ::GlobalLock(hg);
            if (p != NULL)
            {
                ::memcpy(p, m_spec.m_copyText.GetString(), bytes);
                ::GlobalUnlock(hg);
                ::SetClipboardData(CF_UNICODETEXT, hg);
            }
            else
            {
                ::GlobalFree(hg);
            }
        }
        ::CloseClipboard();
    }
    if (m_frame != nullptr)
    {
        m_frame->ShowToast(_T("已复制到剪贴板"));
    }
}

LRESULT ConfirmDialog::OnDuiNotify(UINT, WPARAM, LPARAM lParam,
                                    BOOL& bHandled)
{
    // balloonui 的 WM_DUI_NOTIFY:lParam = const DuiNotify*(含 code+ctrlId+extra)。
    const DuiNotify* n = reinterpret_cast<const DuiNotify*>(lParam);
    if (n == nullptr || n->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    case kIdConfirmCancel:
        // 取消 → 关闭(不通知 AdminFrame 执行待确认操作)。
        RequestClose();
        bHandled = TRUE;
        return 0;
    case kIdConfirmCopy:
        // 复制密码 → Dialog 自己处理,不关闭。
        OnCopyButtonClicked();
        bHandled = TRUE;
        return 0;
    case kIdConfirmOk:
        // 确认 / 知道了 → 通知 AdminFrame 执行待确认操作。AdminFrame 会在
        // 回调里调本对话框的 RequestClose 关闭。
        if (m_frame != nullptr)
        {
            m_frame->OnConfirmDialogConfirmed(this);
        }
        bHandled = TRUE;
        return 0;
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT ConfirmDialog::OnCloseMsg(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    DestroyWindow();
    bHandled = TRUE;
    return 0;
}

LRESULT ConfirmDialog::OnDestroyMsg(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    if (m_parentHwnd != NULL && ::IsWindow(m_parentHwnd))
    {
        ::EnableWindow(m_parentHwnd, TRUE);
        ::SetForegroundWindow(m_parentHwnd);
    }
    if (m_frame != nullptr)
    {
        m_frame->OnConfirmDialogDestroyed(this);
    }
    bHandled = FALSE;   // 让 DuiFrameWindow 的 WM_DESTROY 清理继续执行
    return 0;
}

} // namespace flamingoadmin
