/**
 *  BalloonChangePwdDlg.cpp —— "修改密码"对话框实现。
 *
 *  控件树用 balloonui 现成控件程序化组合（对照 design/修改密码.png）：每个字段 =
 *  DuiLabel(标题) + DuiEdit(密码框，左锁图标 + 右小眼睛 + 掩码)；提示框 =
 *  DuiVBox(琥珀底 + 圆角) 内嵌 DuiLabel(自动换行)；底部 = DuiButton(取消/保存)。
 *  保存流程：读三框 → ValidateChangePwdInput 校验 → Sha256 哈希 → CIUSocket
 *  发 cmd_change_password；应答经主面板转发到 OnChangePwdResult。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "BalloonChangePwdDlg.h"

#include <memory>
#include <string>
#include <vector>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../resource.h"                  // IDR_MAINFRAME（标题栏程序图标）
#include "MainPanelIcons.h"               // DrawLineIcon / kIconLock（密码框左侧锁图标）
#include "BalloonChangePwdValidate.h"     // ValidateChangePwdInput
#include "../../net/Sha256.h"                // 口令 SHA-256 哈希
#include "../../net/AccountProtocolClient.h" // net::ChangePasswordResponse
#include "../../net/IUSocket.h"              // CIUSocket::SendChangePassword
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

// ---- 对话框固定尺寸（逻辑像素）-----------------------------------------
static const int kDlgWidth      = 460;   // 客户区宽
static const int kDlgHeight     = 356;   // 客户区高（按下方行高/间距算出）
static const int kDlgMinWidth   = 400;
static const int kDlgMinHeight  = 340;
static const int kDlgTitleBarH  = 36;

// ---- 控件 id（对话框内唯一即可）----------------------------------------
static const UINT kIdOldPwd     = 1;   // 当前密码输入框
static const UINT kIdNewPwd     = 2;   // 新密码输入框
static const UINT kIdConfirmPwd = 3;   // 确认新密码输入框
static const UINT kIdCancel     = 4;   // 取消按钮
static const UINT kIdSave       = 5;   // 保存按钮

// ---- 行高 / 间距（逻辑像素，整体较初版调小一档）------------------------
static const int kFieldLabelH   = 16;   // 字段标题行高
static const int kEditRowH      = 34;   // 密码输入框高
static const int kFieldGap      = 5;    // 标题与输入框间距
static const int kGroupH        = kFieldLabelH + kFieldGap + kEditRowH;  // 一个字段组总高 = 55
static const int kHintH         = 64;   // 琥珀提示框高
static const int kBottomBarH    = 34;   // 底部按钮栏高（= 按钮高）
static const int kRootPadding   = 22;   // 根容器内边距
static const int kRootGap       = 12;   // 根容器子项间距
static const int kLockGutterW   = 28;   // 密码框左侧锁图标占位宽
static const int kBtnWidth      = 78;   // 取消/保存按钮宽

// ---- 标题栏 --------------------------------------------------------------
// 标题栏背景色（须与 DuiFrameWindow 标题条填充色一致）。标题图标用 SRCCOPY
// 绘制、不做 alpha 混合，故把程序图标合成到此底色上，透明区才与标题栏一致。
static const COLORREF kTitleBarBgColor = RGB(245, 246, 250);
// 标题栏图标边长（与 DuiFrameWindow 标题条绘制的 16x16 一致）。
static const int kTitleIconSize = 16;

// ---- 配色 --------------------------------------------------------------
static const COLORREF kFieldLabelColor = RGB(0x3A, 0x3A, 0x3A);   // 字段标题深灰
static const COLORREF kFieldBgColor    = RGB(0xF4, 0xF3, 0xF0);   // 输入框浅灰底（贴合设计）
static const COLORREF kHintBgColor     = RGB(0xFD, 0xF3, 0xE7);   // 琥珀提示框底（浅橙）
static const COLORREF kHintTextColor   = RGB(0x8A, 0x6E, 0x52);   // 琥珀提示框文字（暖棕灰）
static const COLORREF kLockIconColor   = RGB(0x9A, 0x9A, 0x9A);   // 锁图标灰

// 改密应答里"旧密码错误"的结果码（对应服务端 error_code_wrong_password）。
static const int kRespCodeWrongOldPwd = 103;

// 锁图标描边宽度（viewBox 24 基准）与边长（与 DuiEdit gutter 匹配）。
static const Gdiplus::REAL kLockIconSize   = 16.0f;
static const Gdiplus::REAL kLockStrokeW    = 2.0f;

// =======================================================================
// 文件作用域辅助
// =======================================================================

// CString(UTF-16) → UTF-8 std::string。空串安全（与 CreateGroupDlg 同口径）。
static std::string CStrToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

// 密码框左侧 gutter 的锁图标 painter（自由函数，避免用 lambda）。rc 为 host
// 客户区坐标的 gutter 矩形；把锁图标在其内居中描边绘制。
static void PaintLockGutter(HDC hdc, const RECT& rc)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::REAL x = (Gdiplus::REAL)rc.left
                    + ((Gdiplus::REAL)(rc.right - rc.left) - kLockIconSize) / 2.0f;
    Gdiplus::REAL y = (Gdiplus::REAL)rc.top
                    + ((Gdiplus::REAL)(rc.bottom - rc.top) - kLockIconSize) / 2.0f;

    Gdiplus::Color color(255, GetRValue(kLockIconColor),
                         GetGValue(kLockIconColor), GetBValue(kLockIconColor));
    balloonmain::DrawLineIcon(g, balloonmain::kIconLock, x, y,
                              kLockIconSize, color, kLockStrokeW);
}

// 把 HICON 合成到 sizePx×sizePx 的位图上、底色填 bgColor，返回 HBITMAP。
// DuiFrameWindow 标题栏用 SRCCOPY（不混合 alpha）绘图标，故须自带与标题栏一致
// 的底色：DrawIconEx(DI_NORMAL) 会把图标按其掩码/alpha 合成到这层底色上，
// 透明区即呈现底色。返回的 HBITMAP 所有权归调用方，用完 DeleteObject。
static HBITMAP MakeTitleIconBitmap(HICON hIcon, int sizePx, COLORREF bgColor)
{
    if (hIcon == NULL || sizePx <= 0)
    {
        return NULL;
    }

    HDC     screen = ::GetDC(NULL);
    HDC     mem    = ::CreateCompatibleDC(screen);
    HBITMAP hbmp   = ::CreateCompatibleBitmap(screen, sizePx, sizePx);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || hbmp == NULL)
    {
        if (mem != NULL)
        {
            ::DeleteDC(mem);
        }
        if (hbmp != NULL)
        {
            ::DeleteObject(hbmp);
        }
        return NULL;
    }

    HGDIOBJ old = ::SelectObject(mem, hbmp);

    RECT   rc  = { 0, 0, sizePx, sizePx };
    HBRUSH hbr = ::CreateSolidBrush(bgColor);
    ::FillRect(mem, &rc, hbr);
    ::DeleteObject(hbr);

    // 把图标按掩码 + alpha 合成到已填好的底色上。
    ::DrawIconEx(mem, 0, 0, hIcon, sizePx, sizePx, 0, NULL, DI_NORMAL);

    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    return hbmp;
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonChangePwdDlg::CBalloonChangePwdDlg(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_oldEdit(NULL)
    , m_newEdit(NULL)
    , m_confirmEdit(NULL)
    , m_built(false)
    , m_submitting(false)
    , m_titleIconBmp(NULL)
{
}

CBalloonChangePwdDlg::~CBalloonChangePwdDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

// =======================================================================
// 打开 / 关闭
// =======================================================================

void CBalloonChangePwdDlg::OpenForChange(HWND /*owner*/)
{
    if (!IsWindow())
    {
        // owner 传 NULL：让子控件通知自路由回本对话框（与新建群对话框同处理）。
        if (Create(NULL, CWindow::rcDefault, _T("修改密码"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("修改密码对话框 Create 失败。"));
            return;
        }
        SetTitle(_T("修改密码"));
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   // 只留关闭按钮
        SetMinSize(kDlgMinWidth, kDlgMinHeight);

        // 标题栏图标：用程序图标 IDR_MAINFRAME。合成到标题栏底色上的位图只建一次，
        // 跨"关闭再打开"复用；DuiFrameWindow::SetIcon 仅借用，本类析构时释放。
        if (m_titleIconBmp == NULL)
        {
            HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR,
                                           kTitleIconSize, kTitleIconSize);
            if (hIcon != NULL)
            {
                m_titleIconBmp = MakeTitleIconBitmap(hIcon, kTitleIconSize, kTitleBarBgColor);
                ::DestroyIcon(hIcon);
            }
        }
        if (m_titleIconBmp != NULL)
        {
            SetIcon(m_titleIconBmp);
        }

        m_built = false;
        BuildUiOnce();
        ResizeClient(kDlgWidth, kDlgHeight);
        CenterWindow();
    }

    ResetFields();

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    if (m_oldEdit != NULL)
    {
        m_oldEdit->SetFocus();
    }
    LOG_INFO(_T("修改密码对话框已打开。"));
}

LRESULT CBalloonChangePwdDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 关闭对话框 = 取消修改：仅销毁本窗口（对象由主面板持有，可复用）。
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

void CBalloonChangePwdDlg::AddField(balloonwjui::DuiVBox* root, LPCTSTR label,
                                    UINT editId, balloonwjui::DuiEdit** outEdit)
{
    std::unique_ptr<balloonwjui::DuiVBox> group(new balloonwjui::DuiVBox());
    group->SetGap(kFieldGap);

    std::unique_ptr<balloonwjui::DuiLabel> lab(new balloonwjui::DuiLabel());
    lab->SetText(label);
    lab->SetTextColor(kFieldLabelColor);
    lab->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    group->AddChild(std::move(lab), balloonwjui::DuiLayout::Hint().Fixed(kFieldLabelH));

    std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
    edit->SetCtrlId(editId);
    edit->SetPassword(true);
    // 右侧"小眼睛"按钮：点击在密文 / 明文显示之间切换。
    edit->SetShowEyeToggle(true);
    edit->SetBgColor(kFieldBgColor);
    edit->SetIcon(balloonwjui::DuiEdit::LeftIcon, kLockGutterW, &PaintLockGutter);
    *outEdit = edit.get();
    group->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Fixed(kEditRowH));

    root->AddChild(std::move(group), balloonwjui::DuiLayout::Hint().Fixed(kGroupH));
}

void CBalloonChangePwdDlg::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadding);
    root->SetGap(kRootGap);

    // ---- 三个密码字段 ----
    AddField(root.get(), _T("当前密码"),   kIdOldPwd,     &m_oldEdit);
    AddField(root.get(), _T("新密码"),     kIdNewPwd,     &m_newEdit);
    AddField(root.get(), _T("确认新密码"), kIdConfirmPwd, &m_confirmEdit);

    // ---- 琥珀提示框 ----
    {
        std::unique_ptr<balloonwjui::DuiVBox> hint(new balloonwjui::DuiVBox());
        hint->SetBgColor(kHintBgColor);
        hint->SetCornerRadius(8);
        hint->SetPadding(12);

        std::unique_ptr<balloonwjui::DuiLabel> tip(new balloonwjui::DuiLabel());
        tip->SetText(_T("密码至少 8 位，建议包含字母、数字和符号。修改后需在所有设备重新登录。"));
        tip->SetTextColor(kHintTextColor);
        tip->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
        hint->AddChild(std::move(tip), balloonwjui::DuiLayout::Hint().Weight(1));

        root->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kHintH));
    }

    // ---- 底部按钮栏（弹性空白把按钮推到右侧）----
    {
        std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
        bottom->SetGap(10);

        std::unique_ptr<balloonwjui::DuiControl> spacer(new balloonwjui::DuiControl());
        bottom->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
        cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
        cancel->SetCtrlId(kIdCancel);
        cancel->SetText(_T("取消"));
        bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        std::unique_ptr<balloonwjui::DuiButton> save(new balloonwjui::DuiButton());
        save->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        save->SetVariant(balloonwjui::DuiButton::Variant::Primary);
        save->SetCtrlId(kIdSave);
        save->SetText(_T("保存"));
        bottom->AddChild(std::move(save), balloonwjui::DuiLayout::Hint().Fixed(kBtnWidth));

        root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));
    }

    SetClientContent(std::move(root));
    m_built = true;
}

// =======================================================================
// 事件处理
// =======================================================================

LRESULT CBalloonChangePwdDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == balloonwjui::DUIN_CLICK)
    {
        if (pNotify->ctrlId == kIdCancel)
        {
            if (IsWindow())
            {
                DestroyWindow();
            }
            return 0;
        }
        if (pNotify->ctrlId == kIdSave)
        {
            DoSave();
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

// =======================================================================
// 保存 / 复位 / 应答
// =======================================================================

void CBalloonChangePwdDlg::DoSave()
{
    if (m_submitting)
    {
        return;   // 已提交、等待应答，忽略重复点击
    }

    CString oldPwd     = (m_oldEdit != NULL)     ? m_oldEdit->GetText()     : CString();
    CString newPwd     = (m_newEdit != NULL)     ? m_newEdit->GetText()     : CString();
    CString confirmPwd = (m_confirmEdit != NULL) ? m_confirmEdit->GetText() : CString();

    balloonmain::ChangePwdInputResult vr = balloonmain::ValidateChangePwdInput(
        oldPwd.GetString(), newPwd.GetString(), confirmPwd.GetString());
    if (vr != balloonmain::kCpwOk)
    {
        LPCTSTR msg = _T("请检查输入。");
        balloonwjui::DuiEdit* focusTo = NULL;
        switch (vr)
        {
        case balloonmain::kCpwEmptyOld:
            msg = _T("请输入当前密码。");
            focusTo = m_oldEdit;
            break;
        case balloonmain::kCpwEmptyNew:
            msg = _T("请输入新密码。");
            focusTo = m_newEdit;
            break;
        case balloonmain::kCpwTooShort:
            msg = _T("新密码至少 8 位，请重新输入。");
            focusTo = m_newEdit;
            break;
        case balloonmain::kCpwMismatch:
            msg = _T("两次输入的新密码不一致，请重新输入。");
            focusTo = m_confirmEdit;
            break;
        case balloonmain::kCpwSameAsOld:
            msg = _T("新密码不能与当前密码相同。");
            focusTo = m_newEdit;
            break;
        default:
            break;
        }
        ::MessageBox(m_hWnd, msg, _T("修改密码"), MB_OK | MB_ICONINFORMATION);
        if (focusTo != NULL)
        {
            focusTo->SetFocus();
        }
        return;
    }

    // 校验通过：把旧/新口令明文转 UTF-8 字节做 SHA-256（明文不入网络/日志）。
    std::string oldHash = net::Sha256::hexDigest(CStrToUtf8(oldPwd));
    std::string newHash = net::Sha256::hexDigest(CStrToUtf8(newPwd));
    if (oldHash.empty() || newHash.empty())
    {
        ::MessageBox(m_hWnd, _T("内部错误：口令哈希失败，请重试。"),
                     _T("修改密码"), MB_OK | MB_ICONERROR);
        return;
    }

    m_submitting = true;
    CIUSocket::GetInstance().SendChangePassword(oldHash, newHash);
    LOG_INFO(_T("发起修改密码请求（已提交，等待应答）。"));
}

void CBalloonChangePwdDlg::ResetFields()
{
    if (m_oldEdit != NULL)
    {
        m_oldEdit->SetText(_T(""));
    }
    if (m_newEdit != NULL)
    {
        m_newEdit->SetText(_T(""));
    }
    if (m_confirmEdit != NULL)
    {
        m_confirmEdit->SetText(_T(""));
    }
    m_submitting = false;
}

void CBalloonChangePwdDlg::OnChangePwdResult(const net::ChangePasswordResponse& resp)
{
    m_submitting = false;

    // 对话框可能已被用户关闭，MessageBox 的父窗口据此取有效句柄或 NULL。
    HWND hParent = IsWindow() ? m_hWnd : NULL;

    if (resp.code == 0)
    {
        ::MessageBox(hParent, _T("密码修改成功！其它设备需用新密码重新登录。"),
                     _T("修改密码"), MB_OK | MB_ICONINFORMATION);
        ResetFields();
        if (IsWindow())
        {
            DestroyWindow();
        }
        return;
    }

    if (resp.code == kRespCodeWrongOldPwd)
    {
        ::MessageBox(hParent, _T("当前密码不正确，请重新输入。"),
                     _T("修改密码"), MB_OK | MB_ICONWARNING);
        return;
    }

    ::MessageBox(hParent, _T("密码修改失败，请稍后重试。"),
                 _T("修改密码"), MB_OK | MB_ICONWARNING);
}
